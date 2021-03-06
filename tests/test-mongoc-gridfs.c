#include <mongoc.h>
#define MONGOC_INSIDE
#include <mongoc-gridfs-file-private.h>
#undef MONGOC_INSIDE
#include <stdlib.h>
#include <fcntl.h>

#include "test-libmongoc.h"
#include "TestSuite.h"


static char *gTestUri;


static void
test_create (void)
{
   mongoc_gridfs_t *gridfs;
   mongoc_gridfs_file_t *file;
   mongoc_client_t *client;
   bson_error_t error;

   client = mongoc_client_new (gTestUri);
   assert (client);

   gridfs = mongoc_client_get_gridfs (client, "test", "foo", &error);
   assert (gridfs);

   mongoc_gridfs_drop (gridfs, &error);

   file = mongoc_gridfs_create_file (gridfs, NULL);
   assert (file);
   assert (mongoc_gridfs_file_save (file));

   mongoc_gridfs_file_destroy (file);
   mongoc_gridfs_destroy (gridfs);

   mongoc_client_destroy (client);
}


static void
test_list (void)
{
   mongoc_gridfs_t *gridfs;
   mongoc_gridfs_file_t *file;
   mongoc_client_t *client;
   bson_error_t error;
   mongoc_gridfs_file_list_t *list;
   mongoc_gridfs_file_opt_t opt = { 0 };
   bson_t query, child;
   char buf[100];
   int i = 0;

   client = mongoc_client_new (gTestUri);
   assert (client);

   gridfs = mongoc_client_get_gridfs (client, "test", "fs", &error);
   assert (gridfs);

   mongoc_gridfs_drop (gridfs, &error);

   for (i = 0; i < 3; i++) {
      bson_snprintf (buf, sizeof buf, "file.%d", i);
      opt.filename = buf;
      file = mongoc_gridfs_create_file (gridfs, &opt);
      assert (file);
      assert (mongoc_gridfs_file_save (file));
      mongoc_gridfs_file_destroy (file);
   }

   bson_init (&query);
   bson_append_document_begin (&query, "$orderby", -1, &child);
   bson_append_int32 (&child, "filename", -1, 1);
   bson_append_document_end (&query, &child);
   bson_append_document_begin (&query, "$query", -1, &child);
   bson_append_document_end (&query, &child);

   list = mongoc_gridfs_find (gridfs, &query);

   bson_destroy (&query);

   i = 0;
   while ((file = mongoc_gridfs_file_list_next (list))) {
      bson_snprintf (buf, sizeof buf, "file.%d", i++);

      assert (strcmp (mongoc_gridfs_file_get_filename (file), buf) == 0);

      mongoc_gridfs_file_destroy (file);
   }
   assert(i == 3);
   mongoc_gridfs_file_list_destroy (list);

   bson_init (&query);
   bson_append_utf8 (&query, "filename", -1, "file.1", -1);
   file = mongoc_gridfs_find_one (gridfs, &query, &error);
   assert (file);
   assert (strcmp (mongoc_gridfs_file_get_filename (file), "file.1") == 0);
   mongoc_gridfs_file_destroy (file);

   file = mongoc_gridfs_find_one_by_filename (gridfs, "file.1", &error);
   assert (file);
   assert (strcmp (mongoc_gridfs_file_get_filename (file), "file.1") == 0);
   mongoc_gridfs_file_destroy (file);

   mongoc_gridfs_destroy (gridfs);

   mongoc_client_destroy (client);
}


static void
test_create_from_stream (void)
{
   mongoc_gridfs_t *gridfs;
   mongoc_gridfs_file_t *file;
   mongoc_stream_t *stream;
   mongoc_client_t *client;
   bson_error_t error;
   mongoc_fd_t fd;

   client = mongoc_client_new (gTestUri);
   assert (client);

   gridfs = mongoc_client_get_gridfs (client, "test", "fs", &error);
   assert (gridfs);

   mongoc_gridfs_drop (gridfs, &error);

   fd = mongoc_open ("tests/binary/gridfs.dat", O_RDONLY);
   assert (mongoc_fd_is_valid(fd));

   stream = mongoc_stream_unix_new (fd);

   file = mongoc_gridfs_create_file_from_stream (gridfs, stream, NULL);
   assert (file);
   assert (mongoc_gridfs_file_save (file));

   mongoc_gridfs_file_destroy (file);

   mongoc_gridfs_destroy (gridfs);

   mongoc_client_destroy (client);
}


static void
test_read (void)
{
   mongoc_gridfs_t *gridfs;
   mongoc_gridfs_file_t *file;
   mongoc_stream_t *stream;
   mongoc_client_t *client;
   bson_error_t error;
   ssize_t r;
   char buf[10], buf2[10];
   struct iovec iov[] = { { buf, 10 }, { buf2, 10 } };
   mongoc_fd_t fd;

   client = mongoc_client_new (gTestUri);
   assert (client);

   gridfs = mongoc_client_get_gridfs (client, "test", "fs", &error);
   assert (gridfs);

   mongoc_gridfs_drop (gridfs, &error);

   fd = mongoc_open ("tests/binary/gridfs.dat", O_RDONLY);
   assert (mongoc_fd_is_valid(fd));

   stream = mongoc_stream_unix_new (fd);

   file = mongoc_gridfs_create_file_from_stream (gridfs, stream, NULL);
   assert (file);
   assert (mongoc_gridfs_file_save (file));

   r = mongoc_gridfs_file_readv (file, iov, 2, 20, 0);
   assert (r == 20);
   assert (memcmp (iov[0].iov_base, "Bacon ipsu", 10) == 0);
   assert (memcmp (iov[1].iov_base, "m dolor si", 10) == 0);

   mongoc_gridfs_file_destroy (file);

   mongoc_gridfs_destroy (gridfs);

   mongoc_client_destroy (client);
}


static void
test_write (void)
{
   mongoc_gridfs_t *gridfs;
   mongoc_gridfs_file_t *file;
   mongoc_client_t *client;
   bson_error_t error;
   ssize_t r;
   char buf[] = "foo bar";
   char buf2[] = " baz";
   char buf3[1000];
   mongoc_gridfs_file_opt_t opt = { 0 };

   struct iovec iov[] =
   { { buf, sizeof (buf) - 1 }, { buf2, sizeof (buf2) - 1 } };
   struct iovec riov = { buf3, sizeof (buf3) };
   int len = sizeof buf + sizeof buf2 - 2;

   opt.chunk_size = 2;

   client = mongoc_client_new (gTestUri);
   assert (client);

   gridfs = mongoc_client_get_gridfs (client, "test", "fs", &error);
   assert (gridfs);

   mongoc_gridfs_drop (gridfs, &error);

   file = mongoc_gridfs_create_file (gridfs, &opt);
   assert (file);
   assert (mongoc_gridfs_file_save (file));

   r = mongoc_gridfs_file_writev (file, iov, 2, 0);
   assert (r == len);
   assert (mongoc_gridfs_file_save (file));

   r = mongoc_gridfs_file_seek (file, 0, SEEK_SET);
   assert (!r);

   r = mongoc_gridfs_file_tell (file);
   assert (r == 0);

   r = mongoc_gridfs_file_readv (file, &riov, 1, len, 0);
   assert (r == len);
   assert (memcmp (buf3, "foo bar baz", len) == 0);

   mongoc_gridfs_file_destroy (file);

   mongoc_gridfs_destroy (gridfs);

   mongoc_client_destroy (client);
}


static void
test_stream (void)
{
   mongoc_gridfs_t *gridfs;
   mongoc_gridfs_file_t *file;
   mongoc_client_t *client;
   mongoc_stream_t *stream;
   mongoc_stream_t *in_stream;
   bson_error_t error;
   mongoc_fd_t fd;
   ssize_t r;
   char buf[4096];
   struct iovec iov = { buf, sizeof buf };

   client = mongoc_client_new (gTestUri);
   assert (client);

   gridfs = mongoc_client_get_gridfs (client, "test", "fs", &error);
   assert (gridfs);

   mongoc_gridfs_drop (gridfs, &error);

   fd = mongoc_open ("tests/binary/gridfs.dat", O_RDONLY);
   assert (mongoc_fd_is_valid(fd));

   in_stream = mongoc_stream_unix_new (fd);

   file = mongoc_gridfs_create_file_from_stream (gridfs, in_stream, NULL);
   assert (file);
   assert (mongoc_gridfs_file_save (file));

   stream = mongoc_stream_gridfs_new (file);

   r = mongoc_stream_readv (stream, &iov, 1, file->length, 0);
   assert (r == file->length);

   /* cleanup */
   mongoc_stream_destroy (stream);

   mongoc_gridfs_destroy (gridfs);
   mongoc_client_destroy (client);
}


static void
cleanup_globals (void)
{
   bson_free (gTestUri);
}


void
test_gridfs_install (TestSuite *suite)
{
   gTestUri = bson_strdup_printf ("mongodb://%s/", MONGOC_TEST_HOST);

   TestSuite_Add (suite, "/GridFS/create", test_create);
   TestSuite_Add (suite, "/GridFS/create_from_stream", test_create_from_stream);
   TestSuite_Add (suite, "/GridFS/list", test_list);
   TestSuite_Add (suite, "/GridFS/read", test_read);
   TestSuite_Add (suite, "/GridFS/stream", test_stream);
   TestSuite_Add (suite, "/GridFS/write", test_write);

   atexit (cleanup_globals);
}
