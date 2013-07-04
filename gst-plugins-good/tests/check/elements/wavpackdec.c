/* GStreamer
 *
 * unit test for wavpackdec
 *
 * Copyright (c) 2006 Sebastian Dröge <slomo@circular-chaos.org>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

#include <unistd.h>

#include <gst/check/gstcheck.h>

/* For ease of programming we use globals to keep refs for our floating
 * src and sink pads we create; otherwise we always have to do get_pad,
 * get_peer, and then remove references in every test function */
static GstPad *mysrcpad, *mysinkpad;

guint8 test_frame[] = {
  0x77, 0x76, 0x70, 0x6B,       /* "wvpk" */
  0x2E, 0x00, 0x00, 0x00,       /* ckSize */
  0x04, 0x04,                   /* version */
  0x00,                         /* track_no */
  0x00,                         /* index_no */
  0x00, 0x64, 0x00, 0x00,       /* total_samples */
  0x00, 0x00, 0x00, 0x00,       /* block_index */
  0x00, 0x64, 0x00, 0x00,       /* block_samples */
  0x05, 0x18, 0x80, 0x04,       /* flags */
  0xFF, 0xAF, 0x80, 0x60,       /* crc */
  0x02, 0x00, 0x03, 0x00,       /* data */
  0x04, 0x00, 0x05, 0x03,
  0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x8A, 0x02,
  0x00, 0x00, 0xFF, 0x7F,
  0x00, 0xE4,
};

static GstStaticPadTemplate sinktemplate = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/x-raw-int, "
        "width = (int) 32, "
        "depth = (int) 16, "
        "channels = (int) 1, "
        "rate = (int) 44100, "
        "endianness = (int) BYTE_ORDER, " "signed = (boolean) true")
    );
static GstStaticPadTemplate srctemplate = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/x-wavpack, "
        "width = (int) 16, "
        "channels = (int) 1, " "rate = (int) 44100, " "framed = (boolean) true")
    );

static GstElement *
setup_wavpackdec (void)
{
  GstElement *wavpackdec;

  GST_DEBUG ("setup_wavpackdec");
  wavpackdec = gst_check_setup_element ("wavpackdec");
  mysrcpad = gst_check_setup_src_pad (wavpackdec, &srctemplate, NULL);
  mysinkpad = gst_check_setup_sink_pad (wavpackdec, &sinktemplate, NULL);
  gst_pad_set_active (mysrcpad, TRUE);
  gst_pad_set_active (mysinkpad, TRUE);

  return wavpackdec;
}

static void
cleanup_wavpackdec (GstElement * wavpackdec)
{
  GST_DEBUG ("cleanup_wavpackdec");
  gst_element_set_state (wavpackdec, GST_STATE_NULL);

  gst_pad_set_active (mysrcpad, FALSE);
  gst_pad_set_active (mysinkpad, FALSE);
  gst_check_teardown_src_pad (wavpackdec);
  gst_check_teardown_sink_pad (wavpackdec);
  gst_check_teardown_element (wavpackdec);
}

GST_START_TEST (test_decode_frame)
{
  GstElement *wavpackdec;
  GstBuffer *inbuffer, *outbuffer;
  GstBus *bus;
  int i;

  wavpackdec = setup_wavpackdec ();

  fail_unless (gst_element_set_state (wavpackdec,
          GST_STATE_PLAYING) == GST_STATE_CHANGE_SUCCESS,
      "could not set to playing");
  bus = gst_bus_new ();

  inbuffer = gst_buffer_new_and_alloc (sizeof (test_frame));
  memcpy (GST_BUFFER_DATA (inbuffer), test_frame, sizeof (test_frame));
  ASSERT_BUFFER_REFCOUNT (inbuffer, "inbuffer", 1);
  GST_BUFFER_TIMESTAMP (inbuffer) = 0;
  gst_buffer_ref (inbuffer);

  gst_element_set_bus (wavpackdec, bus);

  /* should decode the buffer without problems */
  fail_unless_equals_int (gst_pad_push (mysrcpad, inbuffer), GST_FLOW_OK);
  ASSERT_BUFFER_REFCOUNT (inbuffer, "inbuffer", 1);
  gst_buffer_unref (inbuffer);

  outbuffer = GST_BUFFER (buffers->data);

  fail_if (outbuffer == NULL);
  /* uncompressed data should be 102400 bytes */
  fail_unless_equals_int (GST_BUFFER_SIZE (outbuffer), 102400);

  /* and all 102400 bytes must be 0, i.e. silence */
  for (i = 0; i < 102400; i++)
    fail_unless_equals_int (GST_BUFFER_DATA (outbuffer)[i], 0);

  ASSERT_BUFFER_REFCOUNT (outbuffer, "outbuffer", 1);
  gst_buffer_unref (outbuffer);
  outbuffer = NULL;

  g_list_free (buffers);
  buffers = NULL;

  gst_bus_set_flushing (bus, TRUE);
  gst_element_set_bus (wavpackdec, NULL);
  gst_object_unref (GST_OBJECT (bus));
  cleanup_wavpackdec (wavpackdec);
}

GST_END_TEST;

GST_START_TEST (test_decode_frame_with_broken_header)
{
  GstElement *wavpackdec;
  GstBuffer *inbuffer;
  GstBus *bus;
  GstMessage *message;

  wavpackdec = setup_wavpackdec ();

  fail_unless (gst_element_set_state (wavpackdec,
          GST_STATE_PLAYING) == GST_STATE_CHANGE_SUCCESS,
      "could not set to playing");
  bus = gst_bus_new ();

  inbuffer = gst_buffer_new_and_alloc (sizeof (test_frame));
  memcpy (GST_BUFFER_DATA (inbuffer), test_frame, sizeof (test_frame));
  /* break header */
  GST_BUFFER_DATA (inbuffer)[2] = 'e';
  ASSERT_BUFFER_REFCOUNT (inbuffer, "inbuffer", 1);
  GST_BUFFER_TIMESTAMP (inbuffer) = 0;
  gst_buffer_ref (inbuffer);

  gst_element_set_bus (wavpackdec, bus);

  /* should fail gracefully */
  fail_unless_equals_int (gst_pad_push (mysrcpad, inbuffer), GST_FLOW_ERROR);
  ASSERT_BUFFER_REFCOUNT (inbuffer, "inbuffer", 1);
  gst_buffer_unref (inbuffer);

  fail_if ((message = gst_bus_pop (bus)) == NULL);
  fail_unless_message_error (message, STREAM, DECODE);
  gst_message_unref (message);

  gst_element_set_bus (wavpackdec, NULL);
  gst_object_unref (GST_OBJECT (bus));
  cleanup_wavpackdec (wavpackdec);
}

GST_END_TEST;

GST_START_TEST (test_decode_frame_with_incomplete_frame)
{
  GstElement *wavpackdec;
  GstBuffer *inbuffer;
  GstBus *bus;
  GstMessage *message;

  wavpackdec = setup_wavpackdec ();

  fail_unless (gst_element_set_state (wavpackdec,
          GST_STATE_PLAYING) == GST_STATE_CHANGE_SUCCESS,
      "could not set to playing");
  bus = gst_bus_new ();

  inbuffer = gst_buffer_new_and_alloc (sizeof (test_frame) - 2);
  memcpy (GST_BUFFER_DATA (inbuffer), test_frame, sizeof (test_frame) - 2);
  ASSERT_BUFFER_REFCOUNT (inbuffer, "inbuffer", 1);
  GST_BUFFER_TIMESTAMP (inbuffer) = 0;
  gst_buffer_ref (inbuffer);

  gst_element_set_bus (wavpackdec, bus);

  /* should fail gracefully */
  fail_unless_equals_int (gst_pad_push (mysrcpad, inbuffer), GST_FLOW_ERROR);
  ASSERT_BUFFER_REFCOUNT (inbuffer, "inbuffer", 1);
  gst_buffer_unref (inbuffer);

  fail_if ((message = gst_bus_pop (bus)) == NULL);
  fail_unless_message_error (message, STREAM, DECODE);
  gst_message_unref (message);


  gst_element_set_bus (wavpackdec, NULL);
  gst_object_unref (GST_OBJECT (bus));
  cleanup_wavpackdec (wavpackdec);
}

GST_END_TEST;

static Suite *
wavpackdec_suite (void)
{
  Suite *s = suite_create ("wavpackdec");
  TCase *tc_chain = tcase_create ("general");

  suite_add_tcase (s, tc_chain);
  tcase_add_test (tc_chain, test_decode_frame);
  tcase_add_test (tc_chain, test_decode_frame_with_broken_header);
  tcase_add_test (tc_chain, test_decode_frame_with_incomplete_frame);

  return s;
}

int
main (int argc, char **argv)
{
  int nf;

  Suite *s = wavpackdec_suite ();
  SRunner *sr = srunner_create (s);

  gst_check_init (&argc, &argv);

  srunner_run_all (sr, CK_NORMAL);
  nf = srunner_ntests_failed (sr);
  srunner_free (sr);

  return nf;
}
