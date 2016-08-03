#! /usr/bin/env python

"""All the tests"""

import random
import string
import unittest
from redis import Redis
from pyreBloom import PyreBloom, PyreBloomException


def sample_strings(length, count):
    """Return a set of sample strings"""
    return [''.join(
        random.sample(string.ascii_lowercase, length)).encode('utf-8') for _ in range(count)]


class BaseTest(unittest.TestCase):
    CAPACITY = 10000
    ERROR_RATE = 0.1
    KEY = b'pyreBloomTesting'

    def setUp(self):
        self.bloom = PyreBloom(self.KEY, self.CAPACITY, self.ERROR_RATE)
        self.redis = Redis()

    def tearDown(self):
        """Remove the bloom filter at the provided test key in all databases"""
        databases = int(self.redis.config_get('databases').get('databases', 0))
        for db in range(databases):
            PyreBloom(self.KEY, 1, 0.1, db=db).delete()


class ErrorsTest(BaseTest):
    """Tests about various error conditions"""

    def test_connection_refused(self):
        """In this test I want to make sure that we can catch errors when
        connecting to a redis instance"""
        self.assertRaises(PyreBloomException, PyreBloom,
                          b'pyreBloomTesting', 100, 0.01, port=1234)

    def test_error(self):
        """If we encounter a redis error, we should raise exceptions"""
        self.bloom.delete()
        # If it's the wrong key type, we should see errors. Specifically, this has one
        # of the keys used as a hash instead of a string.
        self.redis.hmset('pyreBloomTesting.0', {'hello': 5})
        self.assertRaises(PyreBloomException, self.bloom.add, b'hello')
        self.assertRaises(PyreBloomException, self.bloom.update, [b'a', b'b'])
        self.assertRaises(PyreBloomException, self.bloom.contains, b'a')
        self.assertRaises(PyreBloomException, self.bloom.intersection, [b'a', b'b'])


class FunctionalityTest(BaseTest):
    def test_delete(self):
        """Make sure that when we delete the bloom filter, we really do"""
        samples = sample_strings(20, 5000)
        self.bloom.update(samples)
        self.bloom.delete()
        self.assertEqual(len(self.bloom.intersection(samples)), 0,
                         'Failed to actually delete filter')

    def test_add(self):
        """Make sure we can add, check existing in a basic way"""
        tests = [b'hello', b'how', b'are', b'you', b'today']
        for test in tests:
            self.bloom.add(test)
        for test in tests:
            self.assertTrue(test in self.bloom)

    def test_update(self):
        """Make sure we can use the extend method to the same effect"""
        tests = [b'hello', b'how', b'are', b'you', b'today']
        self.bloom.update(tests)
        for test in tests:
            self.assertTrue(test in self.bloom)

    def test_intersection(self):
        """Make sure contains returns a list when given a list"""
        tests = [b'hello', b'how', b'are', b'you', b'today']
        self.bloom.update(tests)
        self.assertEqual(tests, self.bloom.intersection(tests))

    def test_two_instances(self):
        """Make sure two bloom filters pointing to the same key work"""
        bloom = PyreBloom(b'pyreBloomTesting', 10000, 0.1)
        tests = [b'hello', b'how', b'are', b'you', b'today']

        # Add them through the first instance
        self.bloom.update(tests)
        self.assertEqual(tests, self.bloom.intersection(tests))

        # Make sure they're accessible through the second instance
        self.assertEqual(tests, bloom.intersection(tests))


class DbTest(BaseTest):
    """Make sure we can select a database"""

    def test_select_db(self):
        """Can instantiate a bloom filter in a separate db"""
        bloom = PyreBloom(self.KEY, self.CAPACITY, self.ERROR_RATE, db=1)

        # After adding key to our db=0 bloom filter, shouldn't see it in our db=0 bloom
        samples = sample_strings(20, 100)
        self.bloom.update(samples)
        self.assertEqual(len(bloom.intersection(samples)), 0)


class AllocationTest(BaseTest):
    """Tests about large allocations"""
    CAPACITY = 200000000
    ERROR_RATE = 0.00001

    def test_size_allocation(self):
        """Make sure we can allocate a bloom filter that would take more than
        512MB (the string size limit in Redis)"""
        included = sample_strings(20, 5000)
        excluded = sample_strings(20, 5000)

        # Add only the included strings
        self.bloom.update(included)
        self.assertEqual(len(included), len(self.bloom.intersection(included)))

        false_positives = self.bloom.intersection(excluded)
        false_rate = float(len(false_positives)) / len(excluded)
        self.assertTrue(false_rate <= 0.00001,
                        'False positive error rate exceeded!')

        # We also need to know that we can access all the keys we need
        self.assertEqual(self.bloom.keys(),
                         [b'pyreBloomTesting.0', b'pyreBloomTesting.1'])


class Accuracytest(BaseTest):
    """Make sure we meet our accuracy expectations for the bloom filter"""

    def test_random(self):
        """Insert some random strings, make sure we don't see another set of
        random strings as in the bloom filter"""
        included = sample_strings(20, 5000)
        excluded = sample_strings(20, 5000)

        # Add only the included strings
        self.bloom.update(included)
        self.assertTrue(len(included) == len(self.bloom.intersection(included)))

        false_positives = self.bloom.intersection(excluded)
        false_rate = float(len(false_positives)) / len(excluded)
        self.assertTrue(false_rate <= 0.1,
                        'False positive error rate exceeded!')


if __name__ == '__main__':
    unittest.main()
