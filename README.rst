pyreBloom-ng
============

| Python library which implements a Redis-backed Bloom filter.
| This is a fork of `pyreBloom <https://github.com/seomoz/pyreBloom>`_, but a bit faster, has a better API and supports Python 2.6+, 3.3+, PyPy 2 and PyPy 3.

Installation
------------

``pyreBloom-ng`` requires ``hiredis`` library, ``Cython`` and a C compiler.

Install hiredis:
::

    # On Mac:
    brew install hiredis

    # On Debian:
    apt-get install libhiredis-dev

    # From source:
    git clone https://github.com/redis/hiredis
    cd hiredis && make && sudo make install

Install the latest stable library version:
::

    pip install pyreBloom-ng

Instantiate a pyreBloom filter, giving it a redis key prefix, a capacity, and an error rate:
::

    from pyreBloom import PyreBloom

    # Important: ALL keys are bytes and NOT unicode strings.
    # Redis doesn't care about unicode at all.
    f = PyreBloom(b'key_prefix', 10000, 0.01)

    # You can find out how many bits this will theoretically consume
    p.bits

    # And how many hashes are needed to satisfy the false positive rate
    p.hashes

Easily add data to a filter using a set-like interface:
::

    # Add one value at a time (slow).
    f.add(b'bytestuff')

    # Or use batch operations (faster).
    data = [os.urandom(8) for _ in range(1024)]
    f.update(data)
    # Alternative: f += data
Now you can perform membership tests:
::

    # Test one value at a time (slow).
    >>> obj = b'\x00\x01\x02'
    >>> obj in f
    True

    # Use batch operations (faster).
    # Note: pyreBloom.intersection() returns a list of values
    # which are found in a Bloom filter. It makes sense when
    # you consider it a set-like operation.
    f.update([b'0', b'1', b'2', b'3', b'4'])
    found = f.intersection([b'3', b'4', b'5', b'6'])
    # Alternative: found = f & [b'3', b'4', b'5', b'6']
    # found is now [b'3', b'4']

License
-------

Both ``pyreBloom`` and ``pyreBloom-ng`` are distributed under the terms of the MIT license.

See the bundled `LICENSE <https://github.com/leovp/pyreBloom-ng/blob/master/LICENSE>`_ file for more details.
