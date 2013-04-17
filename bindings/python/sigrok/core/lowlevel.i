/*
 * This file is part of the sigrok project.
 *
 * Copyright (C) 2013 Martin Ling <martin-sigrok@earth.li>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

%module lowlevel

%include "../../../swig/libsigrok.i"

%{

void sr_datafeed_python_callback(const struct sr_dev_inst *sdi,
        const struct sr_datafeed_packet *packet, void *cb_data)
{
    PyObject *sdi_obj;
    PyObject *packet_obj;
    PyObject *arglist;
    PyObject *result;
    PyGILState_STATE gstate;
    PyObject *python_callback;

    python_callback = (PyObject *) cb_data;
    gstate = PyGILState_Ensure();

    sdi_obj = SWIG_NewPointerObj(SWIG_as_voidptr(sdi),
            SWIGTYPE_p_sr_dev_inst, 0);

    packet_obj = SWIG_NewPointerObj(SWIG_as_voidptr(packet),
            SWIGTYPE_p_sr_datafeed_packet, 0);

    arglist = Py_BuildValue("(OO)", sdi_obj, packet_obj);

    result = PyEval_CallObject(python_callback, arglist);

    Py_XDECREF(arglist);
    Py_XDECREF(sdi_obj);
    Py_XDECREF(packet_obj);
    Py_XDECREF(result);

    PyGILState_Release(gstate);
}

int sr_session_datafeed_python_callback_add(PyObject *cb)
{
    int ret;

    if (!PyCallable_Check(cb))
        return SR_ERR_ARG;
    else {
        ret = sr_session_datafeed_callback_add(
            sr_datafeed_python_callback, cb);
        if (ret == SR_OK)
            Py_XINCREF(cb);
        return ret;
    }
}

PyObject *cdata(const void *data, unsigned long size)
{
#if PY_MAJOR_VERSION < 3
    return PyString_FromStringAndSize(data, size);
#else
    return PyBytes_FromStringAndSize(data, size);
#endif
}

%}

int sr_session_datafeed_python_callback_add(PyObject *cb);

PyObject *cdata(const void *data, unsigned long size);
