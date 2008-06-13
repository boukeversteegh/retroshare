
#include <assert.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <ctype.h>

#include <openssl/bio.h>
#include <openssl/crypto.h>
#include <openssl/evp.h>
#include <openssl/x509.h>
#include <openssl/x509v3.h>
#include <openssl/xPGP.h>
#include <openssl/ssl.h>

#include <openssl/err.h>
#include <openssl/rand.h>
#include <openssl/fips.h>

static BIO *bio_err=NULL;

#include "p3forums.h"

int testForums(p3Forums *forum);

int main(int argc, char **argv)
{

	/* setup SSL */
        bio_err=BIO_new_fp(stderr,BIO_NOCLOSE);


        /* enable memory leak checking unless explicitly disabled */
	CRYPTO_malloc_debug_init();
	CRYPTO_set_mem_debug_options(V_CRYPTO_MDEBUG_ALL);
	CRYPTO_mem_ctrl(CRYPTO_MEM_CHECK_ON);

        SSL_library_init();
	SSL_load_error_strings();


	/* setup p3Forums */
	uint16_t type = 1;
	CacheStrapper *cs = NULL;
	CacheTransfer *cft = NULL;
	std::string srcdir = "./";
	std::string storedir = "./";

	p3Forums *forum = new p3Forums(type, cs, cft, srcdir, storedir);

	testForums(forum);


	/* cleanup */
        CRYPTO_cleanup_all_ex_data();
        ERR_free_strings();
        ERR_remove_state(0);
        EVP_cleanup();
        //CRYPTO_mem_leaks(bio_err);
        if (bio_err != NULL) BIO_free(bio_err);

}


int testForums(p3Forums *forum)
{
	std::string fId1 = forum->createForum(L"Forum 1", L"first forum", RS_DISTRIB_PUBLIC);
	std::string fId2 = forum->createForum(L"Forum 2", L"next first forum", RS_DISTRIB_PRIVATE);

	forum -> tick();
}

