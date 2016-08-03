/* Copyright (c) 2012-2014 SEOmoz
 * Copyright (c) 2016 leovp
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE
 * LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION
 * OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
 * WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#include "bloom.h"
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>

const uint32_t max_bits_per_key = 0xFFFFFFFF;

/* The size of the context error string */
const size_t errstr_size = 128;

int init_pyrebloom(pyrebloomctxt *ctxt, char *key, uint32_t capacity, double error,
    char *host, uint32_t port, char *password, uint32_t db)
    {
    ctxt->capacity = capacity;
    ctxt->bits     = (uint64_t)(-(log(error) * capacity) / (log(2) * log(2)));
    ctxt->hashes   = (uint32_t)(ceil(log(2) * ctxt->bits / capacity));
    ctxt->error    = error;
    ctxt->key      = malloc(strlen(key));
    strncpy(ctxt->key, key, strlen(key));
    ctxt->seeds    = malloc(ctxt->hashes * sizeof(uint32_t));
    ctxt->password = malloc(strlen(password));
    strncpy(ctxt->password, password, strlen(password));

    /* We'll need a certain number of strings here */
    ctxt->num_keys = (uint32_t)(
        ceil((float)(ctxt->bits) / max_bits_per_key));
    ctxt->keys = malloc(ctxt->num_keys * sizeof(char*));
    for (uint32_t i = 0; i < ctxt->num_keys; ++i) {
        size_t length = strlen(key) + 10;
        ctxt->keys[i] = malloc(length);
        snprintf(ctxt->keys[i], length, "%s.%i", key, i);
    }

    /* The implementation here used to rely on srand(1) and then repeated
     * calls to rand(), but I no longer trust that to provide correct behavior
     * when working between different platforms. As such, We'll be using a LCG
     *
     *     http://en.wikipedia.org/wiki/Linear_congruential_generator
     *
     * Hopefully this will be the last of interoperability issues. Note that
     * updating to this version will unfortunately require rebuilding old
     * bloom filters.
     *
     * Our m is implicitly going to be 2^32 by storing the result into a
     * uint32_t */
    uint32_t a = 1664525;
    uint32_t c = 1013904223;
    uint32_t x = 314159265;
    for (uint32_t i = 0; i < ctxt->hashes; ++i) {
        ctxt->seeds[i] = x;
        x = a * x + c;
    }

    // Now for the redis context
    struct timeval timeout = { 1, 500000 };
    ctxt->ctxt = redisConnectWithTimeout(host, port, timeout);
    if (ctxt->ctxt->err != 0) {
        return PYREBLOOM_ERROR;
    }

    /* And now if a password was provided, then */
    if (strlen(password) != 0) {
        redisReply * reply = NULL;
        reply = redisCommand(ctxt->ctxt, "AUTH %s", password);
        if (reply->type == REDIS_REPLY_ERROR) {
            /* We'll copy the error into the context's error string */
            strncpy(ctxt->ctxt->errstr, reply->str, errstr_size);
            freeReplyObject(reply);
            return PYREBLOOM_ERROR;
        } else {
            freeReplyObject(reply);
            return PYREBLOOM_OK;
        }
    } else {
        /* Make sure that we can ping the host */
        redisReply * reply = NULL;
        reply = redisCommand(ctxt->ctxt, "PING");
        if (reply->type == REDIS_REPLY_ERROR) {
            strncpy(ctxt->ctxt->errstr, reply->str, errstr_size);
            freeReplyObject(reply);
            return PYREBLOOM_ERROR;
        }
        /* And now, finally free this reply object */
        freeReplyObject(reply);
    }

    /* Select the appropriate DB */
    redisReply * reply = NULL;
    reply = redisCommand(ctxt->ctxt, "SELECT %i", db);
    if (reply->type == REDIS_REPLY_ERROR) {
        strncpy(ctxt->ctxt->errstr, reply->str, errstr_size);
        freeReplyObject(reply);
        /* Shut it down. */
        free_pyrebloom(ctxt);
        return PYREBLOOM_ERROR;
    }
    freeReplyObject(reply);

    /* If we've made it this far, we're ok. */
    return PYREBLOOM_OK;
}

int free_pyrebloom(pyrebloomctxt *ctxt)
{
    if (ctxt->seeds) {
        free(ctxt->seeds);
    }
    redisFree(ctxt->ctxt);

    return PYREBLOOM_OK;
}

int add_one(pyrebloomctxt *ctxt, const char *data, uint32_t data_size)
{
    uint32_t ct = 0, total = 0, count = 1;
    redisReply *reply = NULL;

    for (uint32_t i = 0; i < ctxt->hashes; ++i) {
        uint64_t hashed_data = hash(data, data_size, ctxt->seeds[i], ctxt->bits);
        redisAppendCommand(ctxt->ctxt, "SETBIT %s %lu 1",
            ctxt->keys[hashed_data / max_bits_per_key], hashed_data % max_bits_per_key);
    }

    for (uint32_t i = 0, ct = 0; i < ctxt->hashes; ++i) {
        /* Make sure that we were able to read a reply. Otherwise, provide
         * an error response */
        if (redisGetReply(ctxt->ctxt, (void**)(&reply)) == REDIS_ERR) {
            strncpy(ctxt->ctxt->errstr, "No pending replies", errstr_size);
            ctxt->ctxt->err = PYREBLOOM_ERROR;
            return PYREBLOOM_ERROR;
        }

        /* Consume and read the response */
        if (reply->type == REDIS_REPLY_ERROR) {
            ctxt->ctxt->err = PYREBLOOM_ERROR;
            strncpy(ctxt->ctxt->errstr, reply->str, errstr_size);
        } else {
            ct += reply->integer;
        }
        freeReplyObject(reply);
    }
    if (ct == ctxt->hashes) {
        total = 1;
    }

    if (ctxt->ctxt->err == PYREBLOOM_ERROR) {
        return PYREBLOOM_ERROR;
    }

    return count - total;
}

int add(pyrebloomctxt *ctxt, const char *data, uint32_t data_size)
{
    for (uint32_t i = 0; i < ctxt->hashes; ++i) {
        uint64_t d = hash(data, data_size, ctxt->seeds[i], ctxt->bits);
        redisAppendCommand(ctxt->ctxt, "SETBIT %s %lu 1",
            ctxt->keys[d / max_bits_per_key], d % max_bits_per_key);
    }

    return PYREBLOOM_OK;
}

int add_complete(pyrebloomctxt *ctxt, uint32_t count)
{
    uint32_t ct = 0, total = 0;
    redisReply * reply = NULL;

    ctxt->ctxt->err = PYREBLOOM_OK;
    for (uint32_t i = 0; i < count; ++i) {
        for (uint32_t j = 0, ct = 0; j < ctxt->hashes; ++j) {
            /* Make sure that we were able to read a reply. Otherwise, provide
             * an error response */
            if (redisGetReply(ctxt->ctxt, (void**)(&reply)) == REDIS_ERR) {
                strncpy(ctxt->ctxt->errstr, "No pending replies", errstr_size);
                ctxt->ctxt->err = PYREBLOOM_ERROR;
                return PYREBLOOM_ERROR;
            }

            /* Consume and read the response */
            if (reply->type == REDIS_REPLY_ERROR) {
                ctxt->ctxt->err = PYREBLOOM_ERROR;
                strncpy(ctxt->ctxt->errstr, reply->str, errstr_size);
            } else {
                ct += reply->integer;
            }
            freeReplyObject(reply);
        }
        if (ct == ctxt->hashes) {
            total += 1;
        }
    }

    if (ctxt->ctxt->err == PYREBLOOM_ERROR) {
        return PYREBLOOM_ERROR;
    }

    return count - total;
}

int check(pyrebloomctxt *ctxt, const char *data, uint32_t data_size)
{
    for (uint32_t i = 0; i < ctxt->hashes; ++i) {
        uint64_t d = hash(data, data_size, ctxt->seeds[i], ctxt->bits);
        redisAppendCommand(ctxt->ctxt, "GETBIT %s %lu",
            ctxt->keys[d / max_bits_per_key], d % max_bits_per_key);
    }

    return PYREBLOOM_OK;
}

int check_next(pyrebloomctxt *ctxt)
{
    int result = 1;
    redisReply *reply = NULL;
    ctxt->ctxt->err = PYREBLOOM_OK;

    for (uint32_t i = 0; i < ctxt->hashes; ++i) {
        if (redisGetReply(ctxt->ctxt, (void**)(&reply)) == REDIS_ERR) {
            strncpy(ctxt->ctxt->errstr, "No pending replies", errstr_size);
            ctxt->ctxt->err = PYREBLOOM_ERROR;
            return PYREBLOOM_ERROR;
        }

        if (reply->type == REDIS_REPLY_ERROR) {
            ctxt->ctxt->err = PYREBLOOM_ERROR;
            strncpy(ctxt->ctxt->errstr, reply->str, errstr_size);
        }
        result = result && reply->integer;
        freeReplyObject(reply);
    }

    if (ctxt->ctxt->err == PYREBLOOM_ERROR) {
        return PYREBLOOM_ERROR;
    }

    return result;
}

int delete(pyrebloomctxt *ctxt)
{
    uint32_t num_keys = (uint32_t)(
        ceil((float)(ctxt->bits) / max_bits_per_key));

    for (uint32_t i = 0; i < num_keys; ++i) {
        freeReplyObject(redisCommand(ctxt->ctxt, "DEL %s", ctxt->keys[i]));
    }

    return PYREBLOOM_OK;
}

/* From murmur.c */
uint64_t MurmurHash64A(const void *key, uint32_t len, uint64_t seed);

uint64_t hash(const char *data, uint32_t data_size, uint64_t seed, uint64_t bits) {
    return MurmurHash64A(data, data_size, seed) % bits;
}

/* It's a little tricky to get cython to include and build this file separately
 * but it's just copy-and-pasted code for the implementation of murmurhash */
#include "murmur.c"
