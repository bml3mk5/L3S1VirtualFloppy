/** @file shell_cmd.h
 *
 * @author Sasaji
 * @brief 
 * @version 0.1
 * @date 2024-10-01
 * 
 * @copyright Copyright (c) Sasaji 2024
 */

#ifndef SHELL_CMD_H
#define SHELL_CMD_H

#include "embedded_cli.h"

extern EmbeddedCli *_cli;

bool shell_cmd_init(void);
void shell_cmd_task(void);
void shell_cmd_usage_header(const char *cmd, const char *subcmd, const char *msg);
bool shell_cmd_is_help_option(uint16_t pos, const char *args);

bool shell_cmd_get_bool(const char *args, uint16_t pos);
bool shell_cmd_get_uint32(const char *args, uint16_t pos, uint32_t *val);

#endif /* SHELL_CMD_H */
