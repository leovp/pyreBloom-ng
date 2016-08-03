import setuptools
from pip.req import parse_requirements
from pip.download import PipSession

ext_files = ['pyreBloom/bloom.c']

kwargs = {}

try:
    from Cython.Distutils import build_ext
    from Cython.Distutils import Extension

    print('Building from Cython')
    ext_files.append('pyreBloom/pyreBloom.pyx')
    kwargs['cmdclass'] = {'build_ext': build_ext}
except ImportError:
    from distutils.core import Extension

    ext_files.append('pyreBloom/pyreBloom.c')
    print('Building from C')

ext_modules = [Extension("pyreBloom", ext_files, libraries=['hiredis'],
                         library_dirs=['/usr/local/lib'],
                         include_dirs=['/usr/local/include'],
                         extra_compile_args=['-std=c99'])]

setuptools.setup(
    name='pyreBloom-ng',
    version='0.0.1',
    url='https://github.com/leovp/pyreBloom-ng',
    license='MIT',

    author='Leonid Runyshkin',
    author_email='runyshkin@gmail.com',

    keywords='bloom filter redis',
    description='Python library which implements a Redis-backed Bloom filter.',
    # long_description=read('README.rst'),

    platforms=['any'],
    ext_modules=ext_modules,

    install_requires=[
        str(req.req) for req in parse_requirements('requirements.txt',
                                                   session=PipSession())
        ],

    tests_require=[
        str(req.req) for req in parse_requirements('requirements_test.txt',
                                                   session=PipSession())
        ],

    classifiers=[
        'Development Status :: 4 - Beta',
        'Intended Audience :: Developers',
        'License :: OSI Approved :: MIT License',
        'Operating System :: OS Independent',
        'Topic :: Software Development :: Libraries',
        'Topic :: Software Development :: Libraries :: Python Modules',
        'Topic :: Utilities',
        'Programming Language :: C',
        'Programming Language :: Cython',
        'Programming Language :: Python',
        'Programming Language :: Python :: 2',
        'Programming Language :: Python :: 2.6',
        'Programming Language :: Python :: 2.7',
        'Programming Language :: Python :: 3',
        'Programming Language :: Python :: 3.3',
        'Programming Language :: Python :: 3.4',
        'Programming Language :: Python :: 3.5',
    ],
)
