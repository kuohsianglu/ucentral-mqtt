/*
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 2 of the License.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program; if not, write to the Free Software
 *   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307, USA.
 *
 *   Copyright (C) 2020 John Crispin <john@phrozen.org> 
 */

#include "mqtt.h"

enum {
	CMD_SERIAL,
	CMD_CMD,
	__CMD_MAX,
};

static const struct blobmsg_policy cmd_policy[__CMD_MAX] = {
	[CMD_SERIAL] = { .name = "serial", .type = BLOBMSG_TYPE_STRING },
	[CMD_CMD] = { .name = "cmd", .type = BLOBMSG_TYPE_STRING },
};

struct cmd_task {
	char path[128];
	const struct task *task;
	struct runqueue_process proc;
};

static struct runqueue runqueue;
static struct blob_buf b;

static void
cmd_run_cb(struct runqueue *q, struct runqueue_task *task)
{
	struct cmd_task *t = container_of(task, struct cmd_task, proc.task);
	pid_t pid;

	pid = fork();
	if (pid < 0)
		return;

	if (pid) {
		runqueue_process_add(q, &t->proc, pid);
		return;
	}

	execlp("/usr/sbin/ucentral_cmd.sh", "/usr/sbin/ucentral_cmd.sh", t->path, NULL);
	exit(1);
}


static const struct runqueue_task_type cmd_type = {
	.run = cmd_run_cb,
	.cancel = runqueue_process_cancel_cb,
	.kill = runqueue_process_kill_cb,
};

static void
cmd_complete(struct runqueue *q, struct runqueue_task *task)
{
	struct cmd_task *t = container_of(task, struct cmd_task, proc.task);
	free(t);
}

void
cmd_run(char *cmd)
{
	static struct blob_attr *tb[__CMD_MAX];
	FILE *fp = NULL;
	char path[128];

	blob_buf_init(&b, 0);
	if (!blobmsg_add_json_from_string(&b, cmd)) {
		ULOG_ERR("received cmd that is not json\n");
		return;
	}
	blobmsg_parse(cmd_policy, __CMD_MAX, tb, blobmsg_data(b.head), blobmsg_data_len(b.head));
	if (!tb[CMD_SERIAL] || !tb[CMD_CMD]) {
		ULOG_ERR("received cmd that is not valid\n");
		return;
	}
	snprintf(path, sizeof(path), "/tmp/ucentral.cmd.%lld", time(NULL));
	fp = fopen(path, "w+");
        if (!fp) {
		ULOG_ERR("failed to open %s\n", path);
		return;
	}
	if (fwrite(cmd, strlen(cmd), 1, fp) == 1) {
		struct cmd_task *t = calloc(1, sizeof(*t));

		strncpy(t->path, path, sizeof(t->path));
		t->proc.task.type = &cmd_type;
		t->proc.task.run_timeout = 15 * 1000;
		t->proc.task.complete = cmd_complete;

		runqueue_task_add(&runqueue, &t->proc.task, false);
	} else {
		ULOG_ERR("failed to write %s\n", path);
	}
	fclose(fp);
}

void
cmd_init(void)
{
	runqueue_init(&runqueue);
	runqueue.max_running_tasks = 1;
}
