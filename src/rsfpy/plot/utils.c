#include <stdio.h>
#define NPY_NO_DEPRECATED_API NPY_1_7_API_VERSION
#include <Python.h>
#include <numpy/arrayobject.h>

static int interp_cross(float *X, float *Y, int n1, int ntrace,
                 float *newX, float *newY) {
    int maxlen = 0;

    for (int i2 = 0; i2 < ntrace; i2++) {
        int count = 0;
        float *yy   = Y     + i2 * n1;  
        float *nxrow = newX + i2 * (2*n1); 
        float *nyrow = newY + i2 * (2*n1); 

        if (yy[0] <= 0.0f) {
            nxrow[count] = X[0];
            nyrow[count] = 0.0f;
            count++;
        }

        for (int i1 = 0; i1 < n1 - 1; i1++) {
            if (yy[i1] > 0.0f) {
                nxrow[count] = X[i1];
                nyrow[count] = yy[i1];
                count++;
            }

            if ((yy[i1] > 0 && yy[i1+1] <= 0) || (yy[i1] <= 0 && yy[i1+1] > 0)) {
                float denom = yy[i1] - yy[i1+1];
                if (denom != 0.0f) {
                    float alpha = yy[i1] / denom;
                    float xcross = X[i1] + alpha * (X[i1+1] - X[i1]);
                    nxrow[count] = xcross;
                    nyrow[count] = 0.0f;
                    count++;
                }
            }
        }

        if (yy[n1-1] > 0.0f) {
            nxrow[count] = X[n1-1];
            nyrow[count] = yy[n1-1];
            count++;
        }

        if (yy[n1-1] > 0.0f) {
            nxrow[count] = X[n1-1];
            nyrow[count] = 0.0f;
            count++;
        }

        if (count > maxlen) maxlen = count;
    }

    return maxlen;  
}



static PyObject* py_interp_cross(PyObject* self, PyObject* args) {
    PyObject *X_obj, *Y_obj;
    if (!PyArg_ParseTuple(args, "OO", &X_obj, &Y_obj)) return NULL;

    PyArrayObject *X_in = (PyArrayObject*) PyArray_FROM_OTF(X_obj, NPY_NOTYPE,
                                NPY_ARRAY_IN_ARRAY | NPY_ARRAY_C_CONTIGUOUS);
    PyArrayObject *Y_in = (PyArrayObject*) PyArray_FROM_OTF(Y_obj, NPY_NOTYPE,
                                NPY_ARRAY_IN_ARRAY | NPY_ARRAY_C_CONTIGUOUS);
    if (X_in == NULL || Y_in == NULL) return NULL;

    int xtype = PyArray_TYPE(X_in);
    int ytype = PyArray_TYPE(Y_in);
    if (!((xtype == NPY_FLOAT32 || xtype == NPY_FLOAT64) &&
          (ytype == NPY_FLOAT32 || ytype == NPY_FLOAT64))) {
        PyErr_SetString(PyExc_TypeError, "X and Y must be float32 or float64 arrays");
        Py_DECREF(X_in); Py_DECREF(Y_in);
        return NULL;
    }

    if (PyArray_NDIM(X_in) != 1 || PyArray_NDIM(Y_in) != 2) {
        PyErr_SetString(PyExc_ValueError, "X must be 1D, Y must be 2D");
        Py_DECREF(X_in); Py_DECREF(Y_in);
        return NULL;
    }

    int n1 = (int)PyArray_DIM(Y_in, 0);
    int ntrace = (int)PyArray_DIM(Y_in, 1);

    PyArrayObject *X32 = (PyArrayObject*) PyArray_FROM_OTF((PyObject*)X_in, NPY_FLOAT32,
                                NPY_ARRAY_IN_ARRAY | NPY_ARRAY_F_CONTIGUOUS | NPY_ARRAY_FORCECAST);
    PyArrayObject *Y32 = (PyArrayObject*) PyArray_FROM_OTF((PyObject*)Y_in, NPY_FLOAT32,
                                NPY_ARRAY_IN_ARRAY | NPY_ARRAY_F_CONTIGUOUS | NPY_ARRAY_FORCECAST);

    npy_intp dims[2] = {2*n1, ntrace};

    PyArrayObject *newX32 = (PyArrayObject*) PyArray_New(&PyArray_Type, 2, dims, NPY_FLOAT32, NULL, NULL, 0, NPY_ARRAY_F_CONTIGUOUS, NULL);
    PyArrayObject *newY32 = (PyArrayObject*) PyArray_New(&PyArray_Type, 2, dims, NPY_FLOAT32, NULL, NULL, 0, NPY_ARRAY_F_CONTIGUOUS, NULL);

    float *X = (float*)PyArray_DATA(X32);
    float *Y = (float*)PyArray_DATA(Y32);
    float *newX = (float*)PyArray_DATA(newX32);
    float *newY = (float*)PyArray_DATA(newY32);

    int maxlen = interp_cross(X, Y, n1, ntrace, newX, newY);

    PyArrayObject *newX_out, *newY_out;
    if (xtype == NPY_FLOAT64 || ytype == NPY_FLOAT64) {
        newX_out = (PyArrayObject*) PyArray_Cast(newX32, NPY_FLOAT64);
        newY_out = (PyArrayObject*) PyArray_Cast(newY32, NPY_FLOAT64);
        Py_DECREF(newX32);
        Py_DECREF(newY32);
    } else {
        newX_out = newX32;
        newY_out = newY32;
    }

    Py_DECREF(X_in);
    Py_DECREF(Y_in);
    Py_DECREF(X32);
    Py_DECREF(Y32);

    return Py_BuildValue("NNi", newX_out, newY_out, maxlen);
}


static PyMethodDef InterpMethods[] = {
    {"interp_cross", py_interp_cross, METH_VARARGS, "Interpolation crossing"},
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

