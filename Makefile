
OBJDIR := build
OBJS := $(addprefix $(OBJDIR)/, console.o log.o main.o pdp8cpu.o pdp8asm.o tty.o)

CC := clang
CFLAGS := -std=c99 -pedantic-errors -Wall -Wextra -g
VPATH = src

$(OBJDIR)/%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

pdp8:	$(OBJS)
	$(CC) $(OBJS) -o $@

console.o: console.c console.h pdp8.h

log.o: log.c log.h pdp8.h

main.o:	main.c pdp8.h

pdp8asm.o: pdp8asm.c pdp8.h

pdp8cpu.o: pdp8cpu.c pdp8.h console.h tty.h

tty.o: tty.c tty.h

.PHONY:	clean
clean:
	rm -f pdp8 $(OBJDIR)/*.o


