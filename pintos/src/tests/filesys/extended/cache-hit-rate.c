/*
 Measures cache hit rate
 */
#include <syscall.h>
#include "tests/lib.h"
#include "tests/main.h"
#include <random.h>

void
test_main(void) {
    //buffer_cache_reset();
    
    char* file_name = "test1";
    int fd;
    
    int file_size = 500;

    CHECK (create (file_name, file_size), "create \"%s\"", file_name);    
    CHECK ((fd = open (file_name)) > 1, "open \"%s\"", file_name);
    
    buffer_cache_reset();

    char buff[file_size];
    
    int num_read;
    
    int chunk_size = 64;
    int index;
    
    int num_cache_hits_cold = 0;
    
    //End condition ensures a multiple of chunk size when reading
    for (index = 0; index < file_size / chunk_size * chunk_size; index += chunk_size) {
        CHECK((num_read = read(fd, buff + index, chunk_size)) == chunk_size, "read \"%s\"", file_name);
        num_cache_hits_cold += buffer_cache_hit() ? 1 : 0;
    }
    if (file_size % chunk_size != 0) {
        CHECK((num_read = read(fd, buff + index, file_size % chunk_size)) == file_size % chunk_size, "read \"%s\"", file_name);
        num_cache_hits_cold += buffer_cache_hit() ? 1 : 0;
    }
    
    close(fd);
    
    CHECK ((fd = open (file_name)) > 1, "open \"%s\"", file_name);
    
    
    int num_cache_hits_hot = 0;
    
    //End condition ensures a multiple of chunk size when reading
    for (index = 0; index < file_size / chunk_size * chunk_size; index += chunk_size) {
        CHECK((num_read = read(fd, buff + index, chunk_size)) == chunk_size, "read \"%s\"", file_name);
        num_cache_hits_hot += buffer_cache_hit() ? 1 : 0;
    }
    if (file_size % chunk_size != 0) {
        CHECK((num_read = read(fd, buff + index, file_size % chunk_size)) == file_size % chunk_size, "read \"%s\"", file_name);
        num_cache_hits_hot += buffer_cache_hit() ? 1 : 0;
    }
    
    close(fd);

    msg("Hot hit rate is %d", num_cache_hits_hot);
    msg("Cold hit rate is %d", num_cache_hits_cold);

    //Notice that we do not check percentages; this is unnecessary, since the file does not change over time, so we know for sure that
    //the total number of reads during each open is the same. Thus, if the number of hits during the second open is higher, the overall
    //hit rate must also be higher
    
    CHECK(num_cache_hits_hot > num_cache_hits_cold, "Verifying that cache hit rate has improved...");
    
}
