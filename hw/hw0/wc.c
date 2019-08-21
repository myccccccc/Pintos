#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <ctype.h>
#include <stdlib.h>

void wc(FILE* file)
{
    int line_cnt = 0, word_cnt = 0, char_cnt = 0, hasnotspace = 0;
    char ch;
    while (1)
    {
        ch = getc(file);
        if (ch == EOF)
        {
            if(hasnotspace)
            {
                word_cnt++;
            }
            break;
        }
        else
        {
            char_cnt++;
            if (isspace(ch))
            {
                if (ch == '\n')
                {
                    line_cnt++;
                }
                if (hasnotspace)
                {
                    word_cnt++;
                }
                hasnotspace = 0;
            }
            else
            {
                hasnotspace = 1;
            }
        }
    }
    printf(" %d  %d %d ", line_cnt, word_cnt, char_cnt);
    
}
int main(int argc, char *argv[]) {
    char* in_file_name = NULL;
    //int fd = STDIN_FILENO;
    FILE *file;
    if (argc == 1)
    {
        file = stdin;
        wc(file);
        printf("\n");
    }
    else if (argc == 2)
    {
        in_file_name = argv[1];
        file = fopen(in_file_name, "r");
        wc(file);
        printf("%s\n", in_file_name);
    }
    else
    {
        printf("arguments error\n");
		exit(1);
    }
    return 0;
}