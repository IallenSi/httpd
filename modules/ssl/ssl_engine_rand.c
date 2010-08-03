/* Licensed to the Apache Software Foundation (ASF) under one or more
 * contributor license agreements.  See the NOTICE file distributed with
 * this work for additional information regarding copyright ownership.
 * The ASF licenses this file to You under the Apache License, Version 2.0
 * (the "License"); you may not use this file except in compliance with
 * the License.  You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

/*                      _             _
 *  _ __ ___   ___   __| |    ___ ___| |  mod_ssl
 * | '_ ` _ \ / _ \ / _` |   / __/ __| |  Apache Interface to OpenSSL
 * | | | | | | (_) | (_| |   \__ \__ \ |
 * |_| |_| |_|\___/ \__,_|___|___/___/_|
 *                      |_____|
 *  ssl_engine_rand.c
 *  Random Number Generator Seeding
 */
                             /* ``The generation of random
                                  numbers is too important
                                  to be left to chance.'' */

#include "ssl_private.h"

/*  _________________________________________________________________
**
**  Support for better seeding of SSL library's RNG
**  _________________________________________________________________
*/

static int ssl_rand_choosenum(int, int);
static int ssl_rand_feedfp(apr_pool_t *, apr_file_t *, int);

int ssl_rand_seed(server_rec *s, apr_pool_t *p, ssl_rsctx_t nCtx, char *prefix)
{
    SSLModConfigRec *mc;
    apr_array_header_t *apRandSeed;
    ssl_randseed_t *pRandSeeds;
    ssl_randseed_t *pRandSeed;
    unsigned char stackdata[256];
    int nDone;
    apr_file_t *fp;
    int i, n, l;

    mc = myModConfig(s);
    nDone = 0;
    apRandSeed = mc->aRandSeed;
    pRandSeeds = (ssl_randseed_t *)apRandSeed->elts;
    for (i = 0; i < apRandSeed->nelts; i++) {
        pRandSeed = &pRandSeeds[i];
        if (pRandSeed->nCtx == nCtx) {
            if (pRandSeed->nSrc == SSL_RSSRC_FILE) {
                /*
                 * seed in contents of an external file
                 */
                if (apr_file_open(&fp, pRandSeed->cpPath,
                                  APR_READ, APR_OS_DEFAULT, p) != APR_SUCCESS)
                    continue;
                nDone += ssl_rand_feedfp(p, fp, pRandSeed->nBytes);
                apr_file_close(fp);
            }
            else if (pRandSeed->nSrc == SSL_RSSRC_EXEC) {
                const char *cmd = pRandSeed->cpPath;
                const char **argv = apr_palloc(p, sizeof(char *) * 3);
                /*
                 * seed in contents generated by an external program
                 */
                argv[0] = cmd;
                argv[1] = apr_itoa(p, pRandSeed->nBytes);
                argv[2] = NULL;

                if ((fp = ssl_util_ppopen(s, p, cmd, argv)) == NULL)
                    continue;
                nDone += ssl_rand_feedfp(p, fp, pRandSeed->nBytes);
                ssl_util_ppclose(s, p, fp);
            }
#ifdef HAVE_SSL_RAND_EGD
            else if (pRandSeed->nSrc == SSL_RSSRC_EGD) {
                /*
                 * seed in contents provided by the external
                 * Entropy Gathering Daemon (EGD)
                 */
                if ((n = RAND_egd(pRandSeed->cpPath)) == -1)
                    continue;
                nDone += n;
            }
#endif
            else if (pRandSeed->nSrc == SSL_RSSRC_BUILTIN) {
                struct {
                    time_t t;
                    pid_t pid;
                } my_seed;

                /*
                 * seed in the current time (usually just 4 bytes)
                 */
                my_seed.t = time(NULL);

                /*
                 * seed in the current process id (usually just 4 bytes)
                 */
                my_seed.pid = mc->pid;

                l = sizeof(my_seed);
                RAND_seed((unsigned char *)&my_seed, l);
                nDone += l;

                /*
                 * seed in some current state of the run-time stack (128 bytes)
                 */
                n = ssl_rand_choosenum(0, sizeof(stackdata)-128-1);
                RAND_seed(stackdata+n, 128);
                nDone += 128;

            }
        }
    }
    ap_log_error(APLOG_MARK, APLOG_TRACE2, 0, s,
                 "%sSeeding PRNG with %d bytes of entropy", prefix, nDone);

    if (RAND_status() == 0)
        ap_log_error(APLOG_MARK, APLOG_WARNING, 0, s,
                     "%sPRNG still contains insufficient entropy!", prefix);

    return nDone;
}

#define BUFSIZE 8192

static int ssl_rand_feedfp(apr_pool_t *p, apr_file_t *fp, int nReq)
{
    apr_size_t nDone;
    unsigned char caBuf[BUFSIZE];
    apr_size_t nBuf;
    apr_size_t nRead;
    apr_size_t nTodo;

    nDone = 0;
    nRead = BUFSIZE;
    nTodo = nReq;
    while (1) {
        if (nReq > 0)
            nRead = (nTodo < BUFSIZE ? nTodo : BUFSIZE);
        nBuf = nRead;
        if (apr_file_read(fp, caBuf, &nBuf) != APR_SUCCESS)
            break;
        RAND_seed(caBuf, nBuf);
        nDone += nBuf;
        if (nReq > 0) {
            nTodo -= nBuf;
            if (nTodo <= 0)
                break;
        }
    }
    return nDone;
}

static int ssl_rand_choosenum(int l, int h)
{
    int i;
    char buf[50];

    apr_snprintf(buf, sizeof(buf), "%.0f",
                 (((double)(rand()%RAND_MAX)/RAND_MAX)*(h-l)));
    i = atoi(buf)+1;
    if (i < l) i = l;
    if (i > h) i = h;
    return i;
}

