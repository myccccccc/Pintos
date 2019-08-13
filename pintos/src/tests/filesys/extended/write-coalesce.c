/*
 Tests to ensure that writes to the same sector are handled 
 appropriately by the buffer cache (i.e. avoid device writes)
 */

#include <syscall.h>
#include "tests/lib.h"
#include "tests/main.h"
#include <random.h>
#include <stdbool.h>

//On the order of 128
static bool acceptable_range(int val) {
    return val > 12 && val < 1280;
}

void
test_main(void) {
    
    char* file_name = "test2";
    
    //64 KB = 64 * 2^10 = 65536
    int initial_size = 65536;
    
    CHECK (create (file_name, initial_size), "create \"%s\"", file_name);
    
    int fd;
    CHECK ((fd = open (file_name)) > 1, "open \"%s\"", file_name);
    
    char* a_string = "a";
    
    int index;
    for (index = 0; index < initial_size; index ++) {
        if (write (fd, a_string, 1) != 1) {
            fail("Writing failed at index %d", index);
        }
    }
    
    msg("writing done");

    char a_array[1];

    //Return to the beginning for reading
    seek(fd, 0);
    msg("resetting file position");
    
    for (index = 0; index < initial_size; index ++) {
        if (read (fd, a_array, 1) != 1) {
            fail("Reading failed at index %d", index);
        }
    }

    msg("reading done");
    
    close(fd);

    int write_cnt = get_write_cnt();
    
    CHECK(acceptable_range(write_cnt), "Verifying order of device writes...");
}
