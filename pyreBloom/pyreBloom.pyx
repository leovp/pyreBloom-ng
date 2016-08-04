# Copyright (c) 2011 SEOmoz
# Copyright (c) 2016 leovp
# 
# Permission is hereby granted, free of charge, to any person obtaining
# a copy of this software and associated documentation files (the
# "Software"), to deal in the Software without restriction, including
# without limitation the rights to use, copy, modify, merge, publish,
# distribute, sublicense, and/or sell copies of the Software, and to
# permit persons to whom the Software is furnished to do so, subject to
# the following conditions:
# 
# The above copyright notice and this permission notice shall be
# included in all copies or substantial portions of the Software.
# 
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
# EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
# MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
# NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE
# LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION
# OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
# WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

import math
import random

cimport bloom


class PyreBloomException(Exception):
    """Some sort of exception has happened internally"""
    pass


cdef class PyreBloom(object):
    cdef bloom.pyrebloomctxt context
    cdef bytes               key

    property bits:
        def __get__(self):
            return self.context.bits

    property hashes:
        def __get__(self):
            return self.context.hashes

    def __cinit__(self, key, capacity, error, host=b'127.0.0.1', port=6379,
                  password=b'', db=0):
        if not isinstance(key, bytes):
            raise TypeError('Only byte-strings are allowed!')

        self.key = key
        if bloom.init_pyrebloom(&self.context, self.key, capacity,
                                error, host, port, password, db):
            raise PyreBloomException(self.context.ctxt.errstr)

    def __dealloc__(self):
        bloom.free_pyrebloom(&self.context)

    def delete(self):
        bloom.delete(&self.context)

    def add(self, value):
        if not isinstance(value, bytes):
            raise TypeError('Only byte-strings are allowed!')

        r = bloom.add_one(&self.context, value, len(value))
        if r < 0:
            raise PyreBloomException(self.context.ctxt.errstr)

        return r

    def update(self, values):
        r = [bloom.add(&self.context, value, len(value)) for value in values]
        r = bloom.add_complete(&self.context, len(values))
        if r < 0:
            raise PyreBloomException(self.context.ctxt.errstr)

        return r

    def intersection(self, values):
        r = [bloom.check(&self.context, value, len(value)) for value in values]
        r = [bloom.check_next(&self.context) for _ in range(len(values))]
        if min(r) < 0:
            raise PyreBloomException(self.context.ctxt.errstr)

        return [v for v, included in zip(values, r) if included]

    def contains(self, value):
        bloom.check(&self.context, value, len(value))
        r = bloom.check_next(&self.context)
        if r < 0:
            raise PyreBloomException(self.context.ctxt.errstr)

        return bool(r)

    def __contains__(self, value):
        return self.contains(value)

    def __and__(self, values):
        return self.intersection(values)

    def __iadd__(self, values):
        self.update(values)
        return self

    def keys(self):
        """Return a list of the keys used in this bloom filter"""
        return [self.context.keys[i] for i in range(self.context.num_keys)]
