#include <stdio.h>
#define NPY_NO_DEPRECATED_API NPY_1_7_API_VERSION
#include <Python.h>
#include <numpy/arrayobject.h>

/*
 * 对每条 trace 的正半波进行零交叉插值，生成可用于 polygon 填充的点集。
 *
 * 输入:
 *   X    : [n1]               采样坐标
 *   Y    : [n1, ntrace]       每列一条 trace（Fortran-order）
 *
 * 输出:
 *   newX : [2*n1, ntrace]     插值后坐标（Fortran-order）
 *   newY : [2*n1, ntrace]     插值后振幅（Fortran-order）
 *
 * 返回:
 *   maxlen : 所有 trace 中实际写入点数的最大值
 *
 * 说明:
 *   - 本函数按 Fortran-order 的“按列连续”方式访问:
 *       第 i2 条道的首地址 = base + i2 * n1
 *   - 要求 Y, newX, newY 都是 F-contiguous
 */
static int interp_cross_core(
    const float *X, const float *Y,
    int n1, int ntrace,
    float *newX, float *newY
) {
    int maxlen = 0;

    for (int i2 = 0; i2 < ntrace; i2++) {
        int count = 0;

        /* F-order: 第 i2 列连续 */
        const float *yy = Y + i2 * n1;
        float *nxcol = newX + i2 * (2 * n1);
        float *nycol = newY + i2 * (2 * n1);

        /* 起点若 <= 0，则从零开始，保证 polygon 闭合更自然 */
        if (yy[0] <= 0.0f) {
            nxcol[count] = X[0];
            nycol[count] = 0.0f;
            count++;
        }

        for (int i1 = 0; i1 < n1 - 1; i1++) {
            const float y0 = yy[i1];
            const float y1 = yy[i1 + 1];
            const float x0 = X[i1];
            const float x1 = X[i1 + 1];

            /* 当前点在正半轴上，保留 */
            if (y0 > 0.0f) {
                nxcol[count] = x0;
                nycol[count] = y0;
                count++;
            }

            /* 检查零交叉 */
            if ((y0 > 0.0f && y1 <= 0.0f) ||
                (y0 <= 0.0f && y1 > 0.0f)) {
                const float denom = y0 - y1;
                if (denom != 0.0f) {
                    const float alpha = y0 / denom;
                    const float xcross = x0 + alpha * (x1 - x0);

                    nxcol[count] = xcross;
                    nycol[count] = 0.0f;
                    count++;
                }
            }
        }

        /* 末点若 > 0，则补上末点和落回零点 */
        if (yy[n1 - 1] > 0.0f) {
            nxcol[count] = X[n1 - 1];
            nycol[count] = yy[n1 - 1];
            count++;

            nxcol[count] = X[n1 - 1];
            nycol[count] = 0.0f;
            count++;
        }

        if (count > maxlen) {
            maxlen = count;
        }
    }

    return maxlen;
}


static PyObject* py_interp_cross(PyObject* self, PyObject* args) {
    PyObject *X_obj = NULL, *Y_obj = NULL;
    PyArrayObject *X_in = NULL, *Y_in = NULL;
    PyArrayObject *X32 = NULL, *Y32 = NULL;
    PyArrayObject *newX32 = NULL, *newY32 = NULL;
    PyArrayObject *newX_out = NULL, *newY_out = NULL;

    int xtype, ytype;
    int n1, ntrace, maxlen;
    npy_intp dims[2];

    if (!PyArg_ParseTuple(args, "OO", &X_obj, &Y_obj)) {
        return NULL;
    }

    /* 先读入为 ndarray */
    X_in = (PyArrayObject*)PyArray_FROM_OTF(
        X_obj, NPY_NOTYPE,
        NPY_ARRAY_IN_ARRAY
    );
    Y_in = (PyArrayObject*)PyArray_FROM_OTF(
        Y_obj, NPY_NOTYPE,
        NPY_ARRAY_IN_ARRAY
    );

    if (X_in == NULL || Y_in == NULL) {
        Py_XDECREF(X_in);
        Py_XDECREF(Y_in);
        return NULL;
    }

    /* dtype 检查 */
    xtype = PyArray_TYPE(X_in);
    ytype = PyArray_TYPE(Y_in);

    if (!((xtype == NPY_FLOAT32 || xtype == NPY_FLOAT64) &&
          (ytype == NPY_FLOAT32 || ytype == NPY_FLOAT64))) {
        PyErr_SetString(PyExc_TypeError, "X and Y must be float32 or float64 arrays");
        goto fail;
    }

    /* 维度检查 */
    if (PyArray_NDIM(X_in) != 1) {
        PyErr_SetString(PyExc_ValueError, "X must be a 1D array");
        goto fail;
    }

    if (PyArray_NDIM(Y_in) != 2) {
        PyErr_SetString(PyExc_ValueError, "Y must be a 2D array");
        goto fail;
    }

    n1 = (int)PyArray_DIM(Y_in, 0);
    ntrace = (int)PyArray_DIM(Y_in, 1);

    if ((int)PyArray_DIM(X_in, 0) != n1) {
        PyErr_SetString(PyExc_ValueError, "len(X) must equal Y.shape[0]");
        goto fail;
    }

    /* 转成 float32
     * X: 1D，C/F 无所谓
     * Y: 明确转成 F-order，便于按列连续访问
     */
    X32 = (PyArrayObject*)PyArray_FROM_OTF(
        (PyObject*)X_in, NPY_FLOAT32,
        NPY_ARRAY_IN_ARRAY | NPY_ARRAY_FORCECAST
    );
    Y32 = (PyArrayObject*)PyArray_FROM_OTF(
        (PyObject*)Y_in, NPY_FLOAT32,
        NPY_ARRAY_ALIGNED | NPY_ARRAY_F_CONTIGUOUS | NPY_ARRAY_FORCECAST
    );

    if (X32 == NULL || Y32 == NULL) {
        goto fail;
    }

    dims[0] = 2 * n1;
    dims[1] = ntrace;

    /* 用 ZEROS，避免未初始化垃圾值 */
    newX32 = (PyArrayObject*)PyArray_ZEROS(2, dims, NPY_FLOAT32, 1);
    newY32 = (PyArrayObject*)PyArray_ZEROS(2, dims, NPY_FLOAT32, 1);

    if (newX32 == NULL || newY32 == NULL) {
        goto fail;
    }

    maxlen = interp_cross_core(
        (const float*)PyArray_DATA(X32),
        (const float*)PyArray_DATA(Y32),
        n1, ntrace,
        (float*)PyArray_DATA(newX32),
        (float*)PyArray_DATA(newY32)
    );

    /* 输出类型：若任一输入是 float64，则返回 float64 */
    if (xtype == NPY_FLOAT64 || ytype == NPY_FLOAT64) {
        newX_out = (PyArrayObject*)PyArray_Cast(newX32, NPY_FLOAT64);
        newY_out = (PyArrayObject*)PyArray_Cast(newY32, NPY_FLOAT64);
        if (newX_out == NULL || newY_out == NULL) {
            goto fail;
        }
    } else {
        newX_out = newX32;
        newY_out = newY32;
        Py_INCREF(newX_out);
        Py_INCREF(newY_out);
    }

    Py_DECREF(X_in);
    Py_DECREF(Y_in);
    Py_DECREF(X32);
    Py_DECREF(Y32);
    Py_DECREF(newX32);
    Py_DECREF(newY32);

    return Py_BuildValue("NNi", newX_out, newY_out, maxlen);

fail:
    Py_XDECREF(X_in);
    Py_XDECREF(Y_in);
    Py_XDECREF(X32);
    Py_XDECREF(Y32);
    Py_XDECREF(newX32);
    Py_XDECREF(newY32);
    Py_XDECREF(newX_out);
    Py_XDECREF(newY_out);
    return NULL;
}


static PyMethodDef InterpMethods[] = {
    {"interp_cross", py_interp_cross, METH_VARARGS, "Interpolation crossing for wiggle fill"},
    {NULL, NULL, 0, NULL}
};

static struct PyModuleDef interpmodule = {
    PyModuleDef_HEAD_INIT,
    "rsfpy_utils",
    NULL,
    -1,
    InterpMethods
};

PyMODINIT_FUNC PyInit_rsfpy_utils(void) {
    import_array();
    return PyModule_Create(&interpmodule);
}