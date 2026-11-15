#ifndef TEST_IMAGE_GUARD_H
#define TEST_IMAGE_GUARD_H

/* Register the MCUmgr confirmation observer before Test advertising starts. */
void test_image_guard_init(void);

/* Start the independent five-minute rollback deadline for an unconfirmed image. */
void test_image_guard_start(void);

#endif
