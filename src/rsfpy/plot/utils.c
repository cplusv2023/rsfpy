#include <limits.h>
#include <math.h>

#define NPY_NO_DEPRECATED_API NPY_1_7_API_VERSION
#include <Python.h>
#include <numpy/arrayobject.h>

/*
 * rsfpy_utils.interp_cross(X, Y, polarity=1)
 *
 * Purpose
 * -------
 * Build zero-crossing interpolated half-wave curves for wiggle filling.
 *
 * Inputs
 * ------
 * X        : ndarray, shape (n1,), float32/float64
 *            Axis-1 coordinate, usually time/depth.
 * Y        : ndarray, shape (n1, ntrace), float32/float64
 *            Scaled trace amplitude. Each column is one trace.
 *            This function converts Y to float32 Fortran order internally.
 * polarity : int, optional
 *            +1: keep positive half-waves, where Y > 0.
 *            -1: keep negative half-waves, where Y < 0.
 *
 * Returns
 * -------
 * newX   : ndarray, shape (2*n1, ntrace), float32, Fortran order
 * newY   : ndarray, shape (2*n1, ntrace), float32, Fortran order
 * counts : ndarray, shape (ntrace,), intp
 * maxlen : int
 *
 * Notes
 * -----
 * For trace i2, valid points are:
 *
 *     newX[:counts[i2], i2]
 *     newY[:counts[i2], i2]
 *
 * Unused tail values are filled with NaN. This avoids accidental drawing of
 * fake zeros when Python code accidentally slices by maxlen.
 */

static int selected_half(float y, int polarity) {
    if (polarity >= 0) {
        return y > 0.0f;
    } else {
        return y < 0.0f;
    }
}

static void fill_nan_tail(float *x, float *y, int begin, int end) {
    for (int i = begin; i < end; ++i) {
        x[i] = NAN;
        y[i] = NAN;
    }
}

static int interp_cross_core(
    const float *X,
    const float *Y,
    int n1,
    int ntrace,
    int polarity,
    float *newX,
    float *newY,
    npy_intp *counts
) {
    const int nmax = 2 * n1;
    int maxlen = 0;

    for (int i2 = 0; i2 < ntrace; ++i2) {
        int count = 0;

        /* Fortran order: column i2 is contiguous. */
        const float *yy = Y + (npy_intp)i2 * (npy_intp)n1;
        float *nxcol = newX + (npy_intp)i2 * (npy_intp)nmax;
        float *nycol = newY + (npy_intp)i2 * (npy_intp)nmax;

        if (n1 == 1) {
            if (selected_half(yy[0], polarity)) {
                nxcol[count] = X[0];
                nycol[count] = yy[0];
                ++count;

                nxcol[count] = X[0];
                nycol[count] = 0.0f;
                ++count;
            }
            counts[i2] = (npy_intp)count;
            if (count > maxlen) maxlen = count;
            fill_nan_tail(nxcol, nycol, count, nmax);
            continue;
        }

        /* If the trace starts inside the selected half-wave, explicitly start
         * from the zero baseline at the first sample. This makes the polygon
         * close cleanly at the boundary.
         */
        if (selected_half(yy[0], polarity)) {
            nxcol[count] = X[0];
            nycol[count] = 0.0f;
            ++count;
        }

        for (int i1 = 0; i1 < n1 - 1; ++i1) {
            const float y0 = yy[i1];
            const float y1 = yy[i1 + 1];
            const float x0 = X[i1];
            const float x1 = X[i1 + 1];
            const int s0 = selected_half(y0, polarity);
            const int s1 = selected_half(y1, polarity);

            if (s0) {
                if (count < nmax) {
                    nxcol[count] = x0;
                    nycol[count] = y0;
                    ++count;
                }
            }

            if (s0 != s1) {
                const float denom = y0 - y1;
                float xcross;

                if (denom != 0.0f) {
                    const float alpha = y0 / denom;
                    xcross = x0 + alpha * (x1 - x0);
                } else {
                    xcross = x0;
                }

                if (count < nmax) {
                    nxcol[count] = xcross;
                    nycol[count] = 0.0f;
                    ++count;
                }
            }
        }

        if (selected_half(yy[n1 - 1], polarity)) {
            if (count < nmax) {
                nxcol[count] = X[n1 - 1];
                nycol[count] = yy[n1 - 1];
                ++count;
            }
            if (count < nmax) {
                nxcol[count] = X[n1 - 1];
                nycol[count] = 0.0f;
                ++count;
            }
        }

        counts[i2] = (npy_intp)count;
        if (count > maxlen) maxlen = count;
        fill_nan_tail(nxcol, nycol, count, nmax);
    }

    return maxlen;
}

static PyObject* py_interp_cross(PyObject* self, PyObject* args) {
    PyObject *X_obj = NULL, *Y_obj = NULL;
    PyArrayObject *X_in = NULL, *Y_in = NULL;
    PyArrayObject *X32 = NULL, *Y32 = NULL;
    PyArrayObject *newX32 = NULL, *newY32 = NULL, *counts_arr = NULL;

    int polarity = 1;
    int xtype, ytype;
    int n1, ntrace, maxlen;
    npy_intp n1p, ntracep;
    npy_intp dims2[2];
    npy_intp dim_counts[1];

    if (!PyArg_ParseTuple(args, "OO|i", &X_obj, &Y_obj, &polarity)) {
        return NULL;
    }

    polarity = (polarity >= 0) ? 1 : -1;

    X_in = (PyArrayObject*)PyArray_FROM_OTF(
        X_obj, NPY_NOTYPE, NPY_ARRAY_IN_ARRAY
    );
    Y_in = (PyArrayObject*)PyArray_FROM_OTF(
        Y_obj, NPY_NOTYPE, NPY_ARRAY_IN_ARRAY
    );

    if (X_in == NULL || Y_in == NULL) {
        goto fail;
    }

    xtype = PyArray_TYPE(X_in);
    ytype = PyArray_TYPE(Y_in);

    if (!((xtype == NPY_FLOAT32 || xtype == NPY_FLOAT64) &&
          (ytype == NPY_FLOAT32 || ytype == NPY_FLOAT64))) {
        PyErr_SetString(PyExc_TypeError, "X and Y must be float32 or float64 arrays");
        goto fail;
    }

    if (PyArray_NDIM(X_in) != 1) {
        PyErr_SetString(PyExc_ValueError, "X must be a 1D array");
        goto fail;
    }

    if (PyArray_NDIM(Y_in) != 2) {
        PyErr_SetString(PyExc_ValueError, "Y must be a 2D array with shape (n1, ntrace)");
        goto fail;
    }

    n1p = PyArray_DIM(Y_in, 0);
    ntracep = PyArray_DIM(Y_in, 1);

    if (n1p <= 0) {
        PyErr_SetString(PyExc_ValueError, "Y.shape[0] must be positive");
        goto fail;
    }

    if (n1p > (npy_intp)(INT_MAX / 2)) {
        PyErr_SetString(PyExc_ValueError, "Y.shape[0] is too large");
        goto fail;
    }

    if (ntracep < 0 || ntracep > (npy_intp)INT_MAX) {
        PyErr_SetString(PyExc_ValueError, "Y.shape[1] is invalid or too large");
        goto fail;
    }

    if (PyArray_DIM(X_in, 0) != n1p) {
        PyErr_SetString(PyExc_ValueError, "len(X) must equal Y.shape[0]");
        goto fail;
    }

    n1 = (int)n1p;
    ntrace = (int)ntracep;

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

    dims2[0] = (npy_intp)(2 * n1);
    dims2[1] = (npy_intp)ntrace;
    dim_counts[0] = (npy_intp)ntrace;

    newX32 = (PyArrayObject*)PyArray_EMPTY(2, dims2, NPY_FLOAT32, 1);
    newY32 = (PyArrayObject*)PyArray_EMPTY(2, dims2, NPY_FLOAT32, 1);
    counts_arr = (PyArrayObject*)PyArray_EMPTY(1, dim_counts, NPY_INTP, 0);

    if (newX32 == NULL || newY32 == NULL || counts_arr == NULL) {
        goto fail;
    }

    Py_BEGIN_ALLOW_THREADS
    maxlen = interp_cross_core(
        (const float*)PyArray_DATA(X32),
        (const float*)PyArray_DATA(Y32),
        n1,
        ntrace,
        polarity,
        (float*)PyArray_DATA(newX32),
        (float*)PyArray_DATA(newY32),
        (npy_intp*)PyArray_DATA(counts_arr)
    );
    Py_END_ALLOW_THREADS

    Py_DECREF(X_in);
    Py_DECREF(Y_in);
    Py_DECREF(X32);
    Py_DECREF(Y32);

    return Py_BuildValue("NNNi", newX32, newY32, counts_arr, maxlen);

fail:
    Py_XDECREF(X_in);
    Py_XDECREF(Y_in);
    Py_XDECREF(X32);
    Py_XDECREF(Y32);
    Py_XDECREF(newX32);
    Py_XDECREF(newY32);
    Py_XDECREF(counts_arr);
    return NULL;
}

static PyMethodDef InterpMethods[] = {
    {"interp_cross", py_interp_cross, METH_VARARGS,
     "Zero-crossing half-wave interpolation for wiggle fill.\n\n"
     "interp_cross(X, Y, polarity=1) -> newX, newY, counts, maxlen\n"
     "polarity=1 keeps Y>0; polarity=-1 keeps Y<0."},
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
