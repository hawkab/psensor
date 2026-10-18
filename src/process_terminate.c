/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Small on-demand polkit helper: bind a PID, verify start time, send SIGTERM.
 */
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/syscall.h>
#include <unistd.h>

static bool number(const char *value, unsigned long long *out)
{
	char *end;

	if (!*value || *value == '-')
		return false;
	errno = 0;
	*out = strtoull(value, &end, 10);
	return errno == 0 && *end == '\0';
}

static bool same_process(pid_t pid, unsigned long long expected)
{
	char path[64], buffer[4096], *field, *end;
	unsigned long long start;
	ssize_t count;
	int descriptor;

	snprintf(path, sizeof(path), "/proc/%d/stat", pid);
	descriptor = open(path, O_RDONLY | O_CLOEXEC);
	if (descriptor < 0)
		return false;
	count = read(descriptor, buffer, sizeof(buffer) - 1);
	close(descriptor);
	if (count <= 0)
		return false;
	buffer[count] = '\0';
	field = strrchr(buffer, ')');
	if (!field)
		return false;
	field++;
	while (*field == ' ')
		field++;
	if (*field == 'Z' || *field == 'X')
		return false;
	/* field 3 (state) -> field 22 (start time in boot ticks). */
	for (int i = 0; i < 19; i++) {
		field = strchr(field, ' ');
		if (!field)
			return false;
		while (*field == ' ')
			field++;
	}
	errno = 0;
	start = strtoull(field, &end, 10);
	return errno == 0 && end != field && *end == ' ' && start == expected;
}

int main(int argc, char **argv)
{
	unsigned long long pid, start;
	int descriptor, result, saved_errno;

	if (argc != 3 || !number(argv[1], &pid) || !number(argv[2], &start) ||
	    pid <= 1 || pid > INT_MAX || pid == (unsigned long long)getpid() ||
	    !start)
		return 2;
	if (!same_process((pid_t)pid, start))
		return 3;
#if defined(SYS_pidfd_open) && defined(SYS_pidfd_send_signal)
	descriptor = syscall(SYS_pidfd_open, (pid_t)pid, 0);
	if (descriptor < 0)
		return errno == ESRCH ? 3 : errno == EPERM ? 4 : 5;
	/* Authentication may have taken time: never trust the PID alone. */
	if (!same_process((pid_t)pid, start)) {
		close(descriptor);
		return 3;
	}
	result = syscall(SYS_pidfd_send_signal, descriptor, SIGTERM, NULL, 0);
	saved_errno = errno;
	close(descriptor);
	return result == 0	      ? 0
	       : saved_errno == ESRCH ? 3
	       : saved_errno == EPERM ? 4
				      : 5;
#else
	return 5;
#endif
}
