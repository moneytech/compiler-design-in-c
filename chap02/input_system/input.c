/*-----------------------------------------------------------------------------
 * Input.c: The input system used by LeX-generated lexical analyzers.
 ----------------------------------------------------------------------------*/
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>  /* for open() and read() */
#include <unistd.h> /* for close() */
#include <string.h>
#include <stdbool.h>
#include "tools.h"

/*---------------------------------------------------------------------------
 * Helper functions */
#define min(a,b) ((a) < (b) ? (a) : (b))

/*---------------------------------------------------------------------------*/
#define STDIN 0         /* file descriptor of standard input */
#define MAXLOOK 16      /* maximum amount of lookahead       */
#define MAXLEN 1024     /* maximum lexeme sizes              */
#define BUFSIZE ((3 * MAXLEN) + (2 * MAXLOOK)) /* change *3* only */
#define DANGER (End_buf - MAXLOOK)  /* flush buffer when Next passes this
                                       addresses */
#define END (&Start_buf[BUFSIZE])   /* Just past last char in buf */
#define NO_MORE_CHARS() (Eof_read && Next >= End_buf)

static unsigned char Start_buf[BUFSIZE];   /* input bffer */
static unsigned char *End_buf = END;       /* just past last character */
static unsigned char *Next = END;          /* next input character */
static unsigned char *sMark = END;         /* start of current lexeme */
static unsigned char *eMark = END;         /* end of current lexeme */
static unsigned char *pMark = NULL;        /* start of previous lexeme */
static int pLineno = 0;                    /* Line # of previous lexeme */
static int pLength = 0;                    /* length of previous lexeme */

static int Input_file = STDIN;             /* input file handle */
static int Lineno = 1;                     /* current line number */
static int Mline = 1;                      /* Line # when mark_end() called */
static int Termchar = 0;                   /* Holds the character that was
                                              overwritten by a '\0' when we
                                              null terminated the last lexeme.
                                              */
static bool Eof_read = false;              /* End of file has been read.  It's
                                              possible for this to be true and
                                              for characters to still be in
                                              the input buffer . */

/*---------------------------------------------------------------------------
 * Function prototype */
int ii_flush(bool force);
int ii_fillbuf(unsigned char *starting_at);

/*---------------------------------------------------------------------------
 * Initialization routines. */

int ii_newfile(char *filename)
{
    /* prepare a new input file for reading. If newfile() isn't called before
     * input() or input_line() then stdin is used. The current input file is
     * closed after successfully opening the new one (but stdin isn't closed).
     *
     * Return -1 if the file can't be opend; otherwise, return the file
     * descriptor returned from open(). Note that the old input file won't be
     * closed unless the new file is opened successfully. The error code
     * (errno) generated by the bad open() will still be valid, so you can
     * call perror() to find out what went wrong if you like. At least one
     * free file descriptor must be available when newfile() is called.
     */

    int fd;     /* file descriptor */
    fd = (filename == NULL) ? STDIN : open(filename, O_RDONLY);
    if (fd != -1) {
        /* close the current input file and re-initialize variables */
        if (Input_file != STDIN) {
            close(Input_file);
        }

        Input_file = fd;
        Eof_read = false;

        Next = END;
        sMark = END;
        eMark = END;
        End_buf = END;
        Lineno = 1;
        Mline = 1;
    }

    return fd;
}

/*---------------------------------------------------------------------------
 * access routines and marker movement */
char *ii_text(void)  { return (sMark); }
int ii_length(void)  { return (eMark - sMark); }
int ii_lineno(void)  { return (Lineno); }
char *ii_ptext(void) { return (pMark); }
int ii_plength(void) { return (pLength); }
int ii_plineno(void) { return (pLineno); }

/* move sMark to the current input position(Next) */
char *ii_mark_start()
{
    Mline = Lineno;
    eMark = sMark = Next;
    return sMark;
}


/* move eMark to the current input position(Next) */
char *ii_mark_end()
{
    Mline = Lineno;
    eMark = Next;
    return eMark;
}

/* move the start marker one space to the right */
char *ii_move_start()
{
    if (sMark >= eMark) {
        return NULL;
    } else {
        return ++sMark;
    }
}

/* restores the input pointer to the last end mark. */
char *ii_to_mark()
{
    Lineno = Mline;
    Next = eMark;
    return Next;
}

/* modifiers the previous-lexeme marker to reference the same lexeme as the
 * current-lexem marker */
char *ii_mark_prev()
{
    /* set the pMark. Be careful with this routine. A buffer flush won't go
     * past pMark so, once you've set it, you must move it every time you move
     * sMark. I'm not doing this automatically because I might want to
     * remember the token before last rather than the last one. If
     * ii_mark_prev() is never called, pMark is just ignored and you don't
     * have to worry about it */
    pMark = sMark;
    pLineno = Lineno;
    pLength = eMark - sMark;
    return pMark;
}

/*---------------------------------------------------------------------------
 * The advance function */
int ii_advance()
{
    /* ii_advance() is the real input function. It returns the next character
     * from input and advances past it. The buffer is flushed if the current
     * character is within MAXLOOK characters of the end of the buffer. 0 is
     * returned at end of file. -1 is returned if the buffer can't be flushed.
     * because it's too full. In this case you can call ii_flush(1) to do a
     * buffer flush but you'll loose the current lexeme as a consequence.
     */
    static int been_called = 0;
    if (!been_called) {
        /* push a newline into the empty buffer so that the LeX start-of-line
         * anchor will work on the first input line 
         * Note: a NEWLINE will be appended in front of the first line of *
         * the file.*/
        Next = sMark = eMark = END-1;
        *Next = '\n';
        --Lineno;
        --Mline;
        been_called = 1;
    }

    if (NO_MORE_CHARS()) {
        return 0;
    }

    if (!Eof_read && ii_flush(0) < 0) {
        return -1;
    }

    if (*Next == '\n') {
        Lineno ++;
    }

    return (*Next++);
}

int ii_flush(bool force)
{
    /* Flush the input buffer. Do nothing if the current input character isn't
     * in the danger zone, otherwise move all unread characters to the left
     * end of the buffer and fill the remainder of the buffer. Note that
     * input() fulushes the buffer willy-nilly if you read past the end of
     * buffer. Similarly, input_line() flushes the buffer at the beginning of
     * each line.
     *
     * Either the pMark or sMark(which is smaller) is used as the leftmost
     * edge of the buffer. None of the text to the right of the mark will be
     * lost. Return 1 if everything's ok, -1 if the buffer is so full that it
     * can't be flushed. 0 if we're at end of file. If "force" is true, a
     * buffer flush is forced and the characters already in it are discarded.
     * Don't call this function on a buffer that's been terminated by
     * ii_term().
     */
    int copy_amount, shift_amount;
    unsigned char *left_edge;
    
    if (NO_MORE_CHARS()) {
        return 0;
    }

    if (Eof_read) { /* nothing more to be read */
        return 1;
    }
    
    if (Next >= DANGER || force) {
        left_edge = pMark ? min(sMark, pMark) : sMark;
        shift_amount = left_edge - Start_buf;

        if (shift_amount < MAXLEN) {
            /* if not enough room (should be available for at least one
             * lexeme). */
            if (force == false) {
                return -1;
            }

            /* ignoring all saved lexemes */
            left_edge = ii_mark_start();
            ii_mark_prev();
            shift_amount = left_edge - Start_buf;
        }

        copy_amount = End_buf - left_edge;
        memcpy(Start_buf, left_edge, copy_amount);

        if (!ii_fillbuf(Start_buf + copy_amount)) {
            ferr("INTERNAL ERROR, ii_flush: Buffer full, can't read.\n");
        }

        if (pMark) {
            pMark -= shift_amount;
        }

        sMark -= shift_amount;
        eMark -= shift_amount;
        Next -= shift_amount;
    }

    return 1;
}

/*---------------------------------------------------------------------------*/
int ii_fillbuf(unsigned char *starting_at)
{
    /* Fill the input buffer from starting_at to the end of the buffer. The
     * input file is not closed when EOF is reached. Buffers are read in units
     * of MAXLEN characters; It's an error if that many characters cannot be
     * read (0 is returned in this case). For example, if MAXLEN is 1024, then
     * 1024 characters will be read at a time. The number of characters read
     * is returned. Eof_read is true as soon as the last buffer is read. */
    size_t need;    /* number of bytes required from input */
    size_t got;     /* number of bytes actually read. */

    need = ((END-starting_at) / MAXLEN) * MAXLEN;

    if (need < 0) {
        ferr("INTERNAL ERROR (ii_fillbuf): Bad read-request starting addr.\n");
    }

    if (need == 0) {
        return 0;
    }

    got = read(Input_file, starting_at, need);
    if (got == -1) {
        ferr("Can't read input file.\n");
    }

    End_buf = starting_at + got;

    if (got == 0) {
        Eof_read = 1;
    }

    return got;
}

/*---------------------------------------------------------------------------*/

int ii_look(int n)
{
    /* return the nth character of lookhead, EOF if you try to look past end
     * of file, or 0 if you try to look past either end of the buffer */

    unsigned char *p = Next + (n-1);

    if (Eof_read && p >= End_buf) {
        return EOF;
    }

    return (p < Start_buf || p >= End_buf) ? 0 : *p;
}

int ii_pusback(int n)
{
    /* push n characters back into the input. You can't push past the current
     * sMark. You can, however, push back characters after end of file has
     * been encountered. 
     *
     * 0 is returned if you try to push past the sMark, else 1 is returned.
     * */
    while( --n >= 0 && Next > sMark) {
        if (* --Next == '\n' || !*Next) {
            Lineno --;
        }
    }

    if (Next < eMark) {
        eMark = Next;
        Mline = Lineno;
    }

    return (Next > sMark);
}

/*---------------------------------------------------------------------------
 * support for '\0'-terminated strings */
void ii_term()
{
    Termchar = *Next;
    *Next = '\0';
}

void ii_unterm()
{
    if (Termchar) {
        *Next = Termchar;
        Termchar = '\0';
    }
}

/* analogous to ii_advance except considered '\0'-terminator */
int ii_input()
{
    int ret;
    if (Termchar) {
        ii_unterm();
        ret = ii_advance();
        ii_mark_end();
        ii_term();
    } else {
        ret = ii_advance();
        ii_mark_end();
    }
}

int ii_uninput(unsigned char c)
{
    if (Termchar) {
        ii_unterm();
        if (ii_pusback(1)) {
            *Next = c;
        }
        ii_term();
    } else {
        if (ii_pusback(1)) {
            *Next = c;
            
        }
    }
}

int ii_looahead(int n)
{
    return (n == 1 && Termchar) ? Termchar : ii_look(n);
}

int ii_flushbuf()
{
    if (Termchar) {
        ii_unterm();
    }

    return ii_flush(1);
}
