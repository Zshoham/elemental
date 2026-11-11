/*
 * Sketch: a CPython extension module over Elemental's io contracts.
 *
 * VERIFICATION LEVEL: compile check only. This file is compiled against the
 * CPython headers by the test build. It is not linked into a module or
 * imported, so nothing here has been executed.
 *
 * It is not a binding. It exists to check that ownership and callbacks map
 * onto CPython's rules, in both directions:
 *
 *   1. A C stream owned by a Python object (MemReader).
 *   2. A Python object used from C (write_all() hands a file-like object to
 *      C as an el_writer).
 */
#define PY_SSIZE_T_CLEAN
#include <Python.h>

#include <elemental/io.h>
#include <elemental/io_mem.h>
#include <elemental/status.h>

/* ---- 1. A C stream owned by a Python object ------------------------------
 *
 * Elemental's rule is that the caller provides the stream object and the
 * bytes, and keeps both alive. Here the Python object is that caller:
 *
 *   - the el_mem_reader is stored inline in the Python object, which never
 *     moves, so handles made from it stay valid;
 *   - the bytes are pinned through the buffer protocol for as long as the
 *     Python object lives, and released in tp_dealloc.
 *
 * No Elemental function allocates or frees, so there is no allocator to
 * agree on across the boundary.
 */

typedef struct {
    PyObject_HEAD Py_buffer view; /* view.obj != NULL while the bytes are pinned */
    el_mem_reader reader;
} MemReaderObject;

static int MemReader_init(PyObject *self_object, PyObject *args, PyObject *kwargs)
{
    MemReaderObject *self = (MemReaderObject *)self_object;
    Py_buffer view;

    if (kwargs != NULL && PyDict_GET_SIZE(kwargs) != 0) {
        PyErr_SetString(PyExc_TypeError, "MemReader() takes no keyword arguments");
        return -1;
    }
    if (!PyArg_ParseTuple(args, "y*", &view)) {
        return -1;
    }
    if (self->view.obj != NULL) {
        PyBuffer_Release(&self->view); /* __init__ called twice */
    }
    self->view = view;
    el_mem_reader_init(&self->reader, view.buf, (size_t)view.len);
    return 0;
}

static void MemReader_dealloc(PyObject *self_object)
{
    MemReaderObject *self = (MemReaderObject *)self_object;
    PyTypeObject *type = Py_TYPE(self_object);

    if (self->view.obj != NULL) {
        PyBuffer_Release(&self->view);
    }
    type->tp_free(self_object);
    Py_DECREF(type);
}

/* read(size) -> bytes. Returns b"" at the end of the stream, like io.RawIOBase. */
static PyObject *MemReader_read(PyObject *self_object, PyObject *arg)
{
    MemReaderObject *self = (MemReaderObject *)self_object;

    const Py_ssize_t size = PyLong_AsSsize_t(arg);
    if (size < 0) {
        if (!PyErr_Occurred()) {
            PyErr_SetString(PyExc_ValueError, "size must not be negative");
        }
        return NULL;
    }

    PyObject *bytes = PyBytes_FromStringAndSize(NULL, size);
    if (bytes == NULL) {
        return NULL;
    }

    size_t n = 0;
    const el_status status =
        el_read(el_mem_reader_as_reader(&self->reader), PyBytes_AS_STRING(bytes), (size_t)size, &n);
    if (status != EL_OK && status != EL_ERR_END_OF_STREAM) {
        Py_DECREF(bytes);
        PyErr_SetString(PyExc_OSError, el_status_name(status));
        return NULL;
    }
    /* On a non-OK status n is 0, so the end of the stream is b"". */
    if (_PyBytes_Resize(&bytes, (Py_ssize_t)n) != 0) {
        return NULL;
    }
    return bytes;
}

static PyMethodDef MemReader_methods[] = {
    {"read", MemReader_read, METH_O, "read(size) -> bytes"},
    {NULL, NULL, 0, NULL},
};

static PyType_Slot MemReader_slots[] = {
    {Py_tp_init, MemReader_init},
    {Py_tp_dealloc, MemReader_dealloc},
    {Py_tp_methods, MemReader_methods},
    {0, NULL},
};

static PyType_Spec MemReader_spec = {
    .name = "el_io_sketch.MemReader",
    .basicsize = sizeof(MemReaderObject),
    .flags = Py_TPFLAGS_DEFAULT,
    .slots = MemReader_slots,
};

/* ---- 2. A Python object used from C --------------------------------------
 *
 * The operation below is called by Elemental, synchronously, on the thread
 * that called into this module, so the GIL is held. (A binding that released
 * the GIL around a blocking Elemental call would have to reacquire it here
 * with PyGILState_Ensure.)
 *
 * Elemental's rule for operations is that they must not unwind and that
 * they report failure as a status. A Python exception is not unwinding: it
 * is state. The operation leaves the exception set, returns EL_ERR_IO,
 * and write_all() below turns that back into the original exception.
 */

typedef struct file_sink {
    PyObject *file; /* borrowed: the caller of write_all() holds a reference */
} file_sink;

static el_status file_sink_write(void *ctx, const void *data, size_t len, size_t *n)
{
    file_sink *const sink = ctx;
    const Py_ssize_t offered = len > (size_t)PY_SSIZE_T_MAX ? PY_SSIZE_T_MAX : (Py_ssize_t)len;

    /* "y#" copies the bytes, so Python never holds a pointer into `data`. */
    PyObject *result = PyObject_CallMethod(sink->file, "write", "y#", (const char *)data, offered);
    if (result == NULL) {
        if (PyErr_ExceptionMatches(PyExc_BlockingIOError)) {
            PyErr_Clear();
            return EL_ERR_WOULD_BLOCK;
        }
        return EL_ERR_IO; /* the exception stays set for write_all() */
    }

    /* Raw non-blocking streams return None when nothing could be written. */
    if (result == Py_None) {
        Py_DECREF(result);
        return EL_ERR_WOULD_BLOCK;
    }

    const Py_ssize_t written = PyLong_AsSsize_t(result);
    Py_DECREF(result);
    if (written == -1 && PyErr_Occurred()) {
        return EL_ERR_IO;
    }
    if (written < 0 || written > offered) {
        PyErr_SetString(PyExc_ValueError, "write() returned an impossible count");
        return EL_ERR_IO;
    }
    if (written == 0) {
        return EL_ERR_WOULD_BLOCK;
    }

    *n = (size_t)written;
    return EL_OK;
}

static const el_writer_ops file_sink_ops = {
    .write = file_sink_write,
};

/* write_all(file, data) -> number of bytes written. */
static PyObject *sketch_write_all(PyObject *module, PyObject *args)
{
    (void)module;
    PyObject *file;
    Py_buffer data; /* pins the caller's bytes for the duration of the call */

    if (!PyArg_ParseTuple(args, "Oy*", &file, &data)) {
        return NULL;
    }

    file_sink sink = {.file = file};
    const el_writer writer = {.ctx = &sink, .ops = &file_sink_ops};

    size_t n = 0;
    const el_status status = el_write_all(writer, data.buf, (size_t)data.len, &n);
    PyBuffer_Release(&data);

    if (status == EL_OK) {
        return PyLong_FromSize_t(n);
    }
    if (PyErr_Occurred()) {
        return NULL; /* the exception the Python writer raised */
    }
    if (status == EL_ERR_WOULD_BLOCK) {
        /* BlockingIOError(errno, message, characters_written) */
        PyObject *error = PyObject_CallFunction(PyExc_BlockingIOError, "isn", 0,
                                                "write would block", (Py_ssize_t)n);
        if (error != NULL) {
            PyErr_SetObject(PyExc_BlockingIOError, error);
            Py_DECREF(error);
        }
        return NULL;
    }
    PyErr_SetString(PyExc_OSError, el_status_name(status));
    return NULL;
}

/* ---- Module --------------------------------------------------------------- */

static PyMethodDef sketch_methods[] = {
    {"write_all", sketch_write_all, METH_VARARGS, "write_all(file, data) -> int"},
    {NULL, NULL, 0, NULL},
};

static int sketch_exec(PyObject *module)
{
    PyObject *type = PyType_FromSpec(&MemReader_spec);
    if (type == NULL) {
        return -1;
    }
    if (PyModule_AddObject(module, "MemReader", type) != 0) {
        Py_DECREF(type);
        return -1;
    }
    return 0;
}

static PyModuleDef_Slot sketch_slots[] = {
    {Py_mod_exec, sketch_exec},
    {0, NULL},
};

static struct PyModuleDef sketch_module = {
    PyModuleDef_HEAD_INIT,
    .m_name = "el_io_sketch",
    .m_doc = "Sketch of a CPython extension over Elemental's io contracts.",
    .m_size = 0,
    .m_methods = sketch_methods,
    .m_slots = sketch_slots,
};

PyMODINIT_FUNC PyInit_el_io_sketch(void)
{
    return PyModuleDef_Init(&sketch_module);
}
