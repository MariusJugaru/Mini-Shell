// SPDX-License-Identifier: BSD-3-Clause

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>

#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "cmd.h"
#include "utils.h"

#define READ		0
#define WRITE		1

int ret_cond_1 = -101;
int ret_cond_2;

int redirect_out(const char *out, int io_flags, int STD_FILENO)
{
	int file;
	int file2;
	int file_dup;

	file_dup = dup(STD_FILENO);
	if (io_flags != IO_REGULAR)
		file = open(out, O_WRONLY | O_CREAT | O_APPEND, 0777);
	else
		file = open(out, O_WRONLY | O_CREAT | O_TRUNC, 0777);
	file2 = dup2(file, STD_FILENO);
	close(file);

	DIE(file2 == -1, "redirect out");

	return file_dup;
}

int redirect_in(const char *out)
{
	int file;
	int file2;
	int file_dup;

	file_dup = dup(STDIN_FILENO);

	file = open(out, O_RDONLY, 0777);
	file2 = dup2(file, STDIN_FILENO);

	DIE(file2 == -1, "redirect in");

	close(file);
	return file_dup;
}

static bool shell_pwd(void)
{
	char s[500];

	getcwd(s, 500);

	printf("%s\n", s);
	fflush(NULL);
	return 0;
}

/**
 * Internal change-directory command.
 */
static bool shell_cd(word_t *dir)
{
	/* TODO: Execute cd. */
	if (dir == NULL)
		return 0;
	if (dir->string == NULL)
		return 0;
	int ret_val = chdir(dir->string);

	if (ret_val != 0)
		fprintf(stderr, "no such file or directory\n");
	return ret_val;
}

/**
 * Internal exit/quit command.
 */
static int shell_exit(void)
{
	/* TODO: Execute exit/quit. */
	exit(SHELL_EXIT);

	return SHELL_EXIT; /* TODO: Replace with actual exit code. */
}

/**
 * Parse a simple command (internal, environment variable assignment,
 * external command).
 */
static int parse_simple(simple_command_t *s, int level, command_t *father)
{
	/* TODO: Sanity checks. */

	/* TODO: If builtin command, execute the command. */

	/* TODO: If variable assignment, execute the assignment and return
	 * the exit status.
	 */

	/* TODO: If external command:
	 *   1. Fork new process
	 *     2c. Perform redirections in child
	 *     3c. Load executable in child
	 *   2. Wait for child
	 *   3. Return exit status
	 */

	// redirect output
	int file_dup_in = -101;
	int file_dup_out = -101;
	int file_dup_err = -101;
	int status;
	int ret_status;
	char *full_word;

	if ((s->out != NULL) && (s->err != NULL)) {
		full_word = get_word(s->out);
		remove(full_word);
		free(full_word);

		full_word = get_word(s->out);
		file_dup_out = redirect_out(full_word, IO_OUT_APPEND, STDOUT_FILENO);
		free(full_word);

		full_word = get_word(s->err);
		file_dup_err = redirect_out(full_word, IO_ERR_APPEND, STDERR_FILENO);
		free(full_word);
	} else {
		if (s->out != NULL) {
			full_word = get_word(s->out);
			file_dup_out = redirect_out(full_word, s->io_flags, STDOUT_FILENO);
			free(full_word);
		}
		if (s->err != NULL) {
			full_word = get_word(s->err);
			file_dup_err = redirect_out(full_word, s->io_flags, STDERR_FILENO);
			free(full_word);
		}
	}
	if (s->in != NULL) {
		full_word = get_word(s->in);
		file_dup_in = redirect_in(full_word);
		free(full_word);
	}

	int ret_val = -101;

	char *word = get_word(s->verb);
	char *env;

	if (strcmp(word, s->verb->string) != 0) {
		// Env var
		free(word);
		word_t *word_part = s->verb->next_part->next_part;
		char *env_var;

		while (word_part != NULL) {
			// printf("%s\n", word_part->string);
			env_var = getenv(word_part->string);
			if (env_var != NULL)
				word_part->expand = true;
			word_part = word_part->next_part;
		}
		env = get_word(s->verb->next_part->next_part);

		// printf("%s\n", env);
		setenv(s->verb->string, env, 1);
		free(env);

		ret_val = 0;
	} else {
		free(word);
	}

	if (ret_val != 0) {
		if ((strcmp(s->verb->string, "exit") == 0) || (strcmp(s->verb->string, "quit") == 0))
			ret_val = shell_exit();
		if (strcmp(s->verb->string, "cd") == 0)
			ret_val = shell_cd(s->params);
		if (strcmp(s->verb->string, "pwd") == 0)
			ret_val = shell_pwd();
	}

	if (ret_val != -101) {
		if (file_dup_in != -101) {
			dup2(file_dup_in, STDIN_FILENO);
			close(file_dup_in);
		}
		if (file_dup_out != -101) {
			dup2(file_dup_out, STDOUT_FILENO);
			close(file_dup_out);
		}
		if (file_dup_err != -101) {
			dup2(file_dup_err, STDERR_FILENO);
			close(file_dup_err);
		}

		return ret_val;
	}

	int pid = fork();

	int size;
	int err;
	char **argv;

	char error_message[100] = "> Execution failed for \'";

	strcat(error_message, s->verb->string);
	strcat(error_message, "\'");

	switch (pid) {
	case -1:
		DIE(1, "fork");
		break;
	case 0:
		// child process
		argv = get_argv(s, &size);
		err = execvp(s->verb->string, argv);
		if (err == -1) {
			// Command does not exist
			printf("Execution failed for \'%s\'\n", s->verb->string);
			fflush(NULL);
			free(argv);
			exit(1);
			// execlp("echo", "echo", error_message, NULL);
			return 1;
		}
		break;
	default:
		// parent process
		if (file_dup_in != -101) {
			dup2(file_dup_in, STDIN_FILENO);
			close(file_dup_in);
		}
		if (file_dup_out != -101) {
			dup2(file_dup_out, STDOUT_FILENO);
			close(file_dup_out);
		}
		if (file_dup_err != -101) {
			dup2(file_dup_err, STDERR_FILENO);
			close(file_dup_err);
		}

		waitpid(pid, &status, 0);
		ret_status = WEXITSTATUS(status);
	}
	return ret_status; /* TODO: Replace with actual exit status. */
}

/**
 * Process two commands in parallel, by creating two children.
 */
static bool run_in_parallel(command_t *cmd1, command_t *cmd2, int level,
		command_t *father)
{
	/* TODO: Execute cmd1 and cmd2 simultaneously. */
	int ret_val = 0;
	int pid;
	int ret_status;

	/* TODO: Redirect the output of cmd1 to the input of cmd2. */
	pid = fork();

	switch (pid) {
	case -1:
		DIE(-1, "fork");
		break;
	case 0:
		ret_val = parse_command(cmd1, 0, father);
		exit(ret_val);
	default:
		// printf("PARENT %s\n", c->cmd2->scmd->verb->string);
		ret_val = parse_command(cmd2, 0, father);
		ret_cond_1 = ret_val;
		ret_cond_2 = ret_val;

		waitpid(pid, &ret_status, 0);

		ret_val = WEXITSTATUS(ret_status);
		break;
	}
	return ret_val; /* TODO: Replace with actual exit status. */
}

/**
 * Run commands by creating an anonymous pipe (cmd1 | cmd2).
 */
static bool run_on_pipe(command_t *cmd1, command_t *cmd2, int level,
		command_t *father)
{
	int ret_val = 0;
	int pid;
	int file_dup_in = -101;
	int fd[2];
	int ret_status;

	/* TODO: Redirect the output of cmd1 to the input of cmd2. */
	if (pipe(fd) == -1)
		return 1;
	pid = fork();

	switch (pid) {
	case -1:
		DIE(-1, "fork");
		break;
	case 0:
		// printf("CHILD %s\n", c->cmd1->scmd->verb->string);
		dup2(fd[1], STDOUT_FILENO);
		close(fd[0]);
		close(fd[1]);
		ret_val = parse_command(cmd1, 0, father);
		exit(ret_val);
	default:
		// printf("PARENT %s\n", c->cmd2->scmd->verb->string);
		file_dup_in = dup(STDIN_FILENO);
		dup2(fd[0], STDIN_FILENO);
		close(fd[0]);
		close(fd[1]);
		ret_val = parse_command(cmd2, 0, father);
		ret_cond_1 = ret_val;
		ret_cond_2 = ret_val;

		waitpid(pid, &ret_status, 0);

		ret_val = WEXITSTATUS(ret_status);
		dup2(file_dup_in, STDIN_FILENO);
		close(file_dup_in);
		break;
	}
	return ret_val; /* TODO: Replace with actual exit status. */
}

/**
 * Parse and execute a command.
 */
int parse_command(command_t *c, int level, command_t *father)
{
	/* TODO: sanity checks */
	int ret_val = 0;

	if (father == NULL || c->op == OP_SEQUENTIAL) {
		ret_cond_1 = -101;
		ret_cond_2 = -101;
	}

	if (c->op == OP_NONE) {
		/* TODO: Execute a simple command. */
		ret_val = parse_simple(c->scmd, 0, NULL);

		return ret_val; /* TODO: Replace with actual exit code of command. */
	}

	switch (c->op) {
	case OP_SEQUENTIAL:
		/* TODO: Execute the commands one after the other. */
		ret_val = parse_command(c->cmd1, 0, c);
		// execute right side

		ret_cond_1 = -101;
		ret_cond_2 = -101;
		if (c->cmd2 != NULL)
			ret_val = parse_command(c->cmd2, 0, c);
		break;

	case OP_PARALLEL:
		/* TODO: Execute the commands simultaneously. */
		run_in_parallel(c->cmd1, c->cmd2, 0, c);
		break;

	case OP_CONDITIONAL_NZERO:
		/* TODO: Execute the second command only if the first one
		 * returns non zero.
		 */
		// execute left side
		if (ret_cond_1 != 0 && ret_cond_2 != 0)
			ret_cond_1 = parse_command(c->cmd1, 0, c);
		// execute right side
		if (c->cmd2 != NULL)
			if (ret_cond_1 != 0 && ret_cond_2 != 0)
				ret_cond_1 = parse_command(c->cmd2, 0, c);
		break;

	case OP_CONDITIONAL_ZERO:
		/* TODO: Execute the second command only if the first one
		 * returns zero.
		 */
		// execute left side
		if (ret_cond_2 == 0 || ret_cond_2 == -101)
			ret_cond_2 = parse_command(c->cmd1, 0, c);
		// execute right side
		if (c->cmd2 != NULL)
			if (ret_cond_2 == 0 || ret_cond_2 == -101)
				ret_cond_2 = parse_command(c->cmd2, 0, c);
		break;

	case OP_PIPE:
		/* TODO: Redirect the output of the first command to the
		 * input of the second.
		 */
		run_on_pipe(c->cmd1, c->cmd2, 0, c);
		break;

	default:
		return SHELL_EXIT;
	}
	if (ret_cond_1 != -101)
		ret_val = ret_cond_1;
	if (ret_cond_2 != -101)
		ret_val = ret_cond_2;

	fflush(NULL);
	return ret_val; /* TODO: Replace with actual exit code of command. */
}
