#include "so_stdio.h"
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>
#include <stdio.h>

#define BUFSIZE 4096

struct _so_file {
	unsigned char *buffer;
	int fd;
	int currentPosition;
	int occupied; /* retin numarul de bytes cititi */
	int eofFlag;
	int writeFlag;
	int readFlag;
	int errorFlag;
	long offset;
};

/* deschide (creeaza) fisierul in modul cerut */
SO_FILE *so_fopen(const char *pathname, const char *mode)
{
	int fd = -1;
	SO_FILE *file;

	if (!strcmp(mode, "r"))
		fd = open(pathname, O_RDONLY);

	if (!strcmp(mode, "r+"))
		fd = open(pathname, O_RDWR);

	if (!strcmp(mode, "w"))
		fd = open(pathname, O_WRONLY | O_CREAT | O_TRUNC, 0644);

	if (!strcmp(mode, "w+"))
		fd = open(pathname, O_RDWR | O_CREAT | O_TRUNC, 0644);

	if (!strcmp(mode, "a"))
		fd = open(pathname, O_APPEND | O_WRONLY | O_CREAT, 0644);

	if (!strcmp(mode, "a+"))
		fd = open(pathname, O_APPEND | O_RDWR | O_CREAT, 0644);

	if (fd < 0)
		return NULL;

	/* aloc memoria necesara pentru structura si buffer */
	file = malloc(sizeof(SO_FILE));
	file->buffer = malloc(BUFSIZE * sizeof(char));
	if (!file->buffer) {
		free(file);
		return NULL;
	}

	/* initializez campurile structurii */
	file->fd = fd;
	file->currentPosition = 0;
	file->eofFlag = 0;
	file->writeFlag = 0;
	file->readFlag = 0;
	file->occupied = 0;
	file->offset = 0;
	file->errorFlag = 0;

	return file;
}

int so_fclose(SO_FILE *stream)
{
	int cret, fret = 0;

	fret = so_fflush(stream);
	cret = close(stream->fd);

	/* eliberez memoria alocata */
	free(stream->buffer);
	free(stream);

	if (cret == 0 && fret == 0)
		return 0;

	return SO_EOF;
}

/* intoarce un caracter din buffer daca poate; in caz contrar, apeleaza read */
int so_fgetc(SO_FILE *stream)
{
	int ret, currentPosition, c;

	currentPosition = stream->currentPosition;
	stream->readFlag = 1;

	if (currentPosition == 0 || currentPosition == stream->occupied) {
		ret = read(stream->fd, stream->buffer, BUFSIZE);

		if (ret == 0) {
			stream->eofFlag = SO_EOF;
			return SO_EOF;
		}

		if (ret < 0) {
			stream->errorFlag = 1;
			return SO_EOF;
		}

		stream->occupied = ret;
		stream->currentPosition = 0;
	}

	c = (int)stream->buffer[stream->currentPosition];
	stream->currentPosition++;

	return c;
}

/* citesc datele din stream folosind fgetc */
size_t so_fread(void *ptr, size_t size, size_t nmemb, SO_FILE *stream)
{
	int nr, i;
	unsigned char c;
	char *tmp;

	/* numarul de elemente citite */
	nr = nmemb * size;
	stream->readFlag = 1;
	tmp = ptr;

	for (i = 0; i < nr; ++i) {
		c = (unsigned char) so_fgetc(stream);

		if (so_feof(stream) || so_ferror(stream))
			return i / size;

		memcpy(tmp + i, &c, 1);
	}

	return nmemb;
}

int so_fflush(SO_FILE *stream)
{
	int ret;

	/* daca ultima operatie a fost de read */
	if (stream->readFlag) {
		stream->readFlag = 0;
		return 0;
	}

	/* daca bufferul e gol, nu face nimic */
	if (stream->currentPosition == 0)
		return 0;

	/* ultima operatie a fost write  */
	if (stream->writeFlag && stream->currentPosition != 0) {
		ret = write(stream->fd, stream->buffer,
					stream->currentPosition);

		while (ret < stream->currentPosition) {
			if (ret <= 0) {
				stream->errorFlag = 1;
				return SO_EOF;
			}

			ret += write(stream->fd, stream->buffer + ret,
						stream->currentPosition - ret);
		}

		stream->currentPosition = 0;
		stream->writeFlag = 0;
		return 0;
	}

	stream->errorFlag = 1;
	return SO_EOF;
}

/* memoreaza un caracter in buffer */
int so_fputc(int c, SO_FILE *stream)
{
	int ret, currentPosition;

	currentPosition = stream->currentPosition;
	stream->writeFlag = 1;

	if (!stream)
		return SO_EOF;

	/* daca bufferul e plin => fflush */
	if (currentPosition == BUFSIZE) {
		ret = so_fflush(stream);
		if (ret == SO_EOF)
			return SO_EOF;
	}

	stream->buffer[stream->currentPosition] = c;
	stream->currentPosition++;
	return c;
}

/* scrie date in stream folosind fputc */
size_t so_fwrite(const void *ptr, size_t size, size_t nmemb, SO_FILE *stream)
{
	int nr, i, c, ret;

	nr = nmemb * size;
	stream->writeFlag = 1;

	for (i = 0; i < nr; ++i) {
		c = *((unsigned char *) ptr + i);

		if (c != SO_EOF) {
			ret = so_fputc(c, stream);

			if (ret == SO_EOF)
				return i / size;

			if (so_ferror(stream))
				return 0;
		}
	}

	return nmemb;
}

int so_fileno(SO_FILE *stream)
{
	return stream->fd;
}

int so_fseek(SO_FILE *stream, long offset, int whence)
{
	/* verific pentru whence invalid */
	if (whence != SEEK_CUR && whence != SEEK_SET && whence != SEEK_END) {
		stream->errorFlag = 1;
		return -1;
	}

	/* daca deja am citit date, recalculez offsetul */
	if (whence == SEEK_CUR && stream->readFlag)
		offset -= stream->occupied - stream->currentPosition;

	/* ultima operatie a fost write => fflush */
	if (stream->writeFlag)
		so_fflush(stream);

	/* ultima operatie a fost read => bufferul treabuie invalidat */
	if (stream->readFlag) {
		stream->currentPosition = 0;
		stream->occupied = 0;
		stream->readFlag = 0;
	}

	stream->offset = lseek(stream->fd, offset, whence);

	if (stream->offset < 0)
		return -1;

	return 0;
}

/* intoarce pozitia curenta din fisier folosind ftell */
long so_ftell(SO_FILE *stream)
{
	int ret;

	ret = so_fseek(stream, 0, SEEK_CUR);
	if (ret == -1)
		return -1;

	return stream->offset;
}

int so_feof(SO_FILE *stream)
{
	return stream->eofFlag;
}

int so_ferror(SO_FILE *stream)
{
	return stream->errorFlag;
}

SO_FILE *so_popen(const char *command, const char *type)
{
	return NULL;
}

int so_pclose(SO_FILE *stream)
{
	return 0;
}

