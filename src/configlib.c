/********************************************************
 * cPige2, under GNU/GPL v2. See LICENCE for details
 *
 * http://ed.zehome.com/?page=cpige-en
 *
 * (c) 2007 Laurent Coustet
 ********************************************************/

#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>

#include "tool.h"
#include "configlib.h"
#include "debug.h"
#include "mlvpn.h"

#define MAXLINE 1024

config_t *
_conf_parseConfig(int config_file_fd)
{
    FILE *configFile;
    int size, i = 0;
    int bufsize = 256;
    unsigned int linenum = 0;
    char c;
    char *buf;
    char *newline;
    char *tmp;
    char *section = NULL;

    config_t *config;
    confObj_t *confObj;

    configFile = fdopen(config_file_fd, "r");
    if (! configFile)
    {
        _ERROR("Unable to open config file fd: %d: %s\n",
            config_file_fd, strerror(errno));
        return NULL;
    }
    config = (config_t *)malloc(sizeof(config_t));
    config->next    = NULL;
    config->section = NULL;
    config->conf    = NULL;

    buf = (char *)calloc(bufsize, 1);

    while (! feof(configFile))
    {
        size = fread(&c, 1, 1, configFile);
        if (size <= 0)
        {
            if (feof(configFile))
                break;
            else
            {
                _ERROR("Error reading config file.\n");
                free(config);
                free(buf);
                return NULL;
            }
        }

        if (bufsize < i + 64)
        {
            bufsize += 64;
            buf = realloc(buf, bufsize);
        }

        switch (c)
        {
            case '\r': break; /* Do nothing */
            case '\n':
               linenum++;
               buf[i] = 0;
               newline = _conf_strip_comment(buf, i);

               if (newline)
               {
                   if ( (tmp = _conf_get_section(newline, i, linenum)) != NULL)
                   {
                       if (section != NULL)
                           free(section);

                       section = tmp;
                   } else if ( (confObj = _conf_parseLine(newline,
                                   strlen(newline), linenum)) != NULL)
                   {
                       if (section == NULL)
                       {
                           _ERROR("Parse error near line %d: variables should "
                                   "always been defined in a section!\n", linenum);
                       } else if (_conf_setValue(config, confObj, section) == NULL) {
                           /* Error there, cleanup memory */
                           if (confObj->var)
                               free(confObj->var);
                           if (confObj->val)
                               free(confObj->val);
                           free(confObj);
                       }
                   }
                   free(newline);
               }

               i = 0;
               break;
            default:
               buf[i++] = c;
        }
    }

    if (section)
        free(section);
    free(buf);
    fclose(configFile);
    return config;
}

/*
 * This function will strip comments
 */
char *
_conf_strip_comment(char *line, unsigned int size)
{
    int i, j = 0;
    short quote = 0;
    char c;
    char *new;

    new = calloc(size+1, 1);

    for ( i = 0; i < size; i++)
    {
        c = line[i];

        switch (c)
        {
            case '\"':
                quote ^= 1; /* Nice :) */
                new[j++] = '\"';
                break;

            case '#':
                if (quote == 1)
                {
                    new[j++] = c; /* Avoid counting "http://" */
                    break;
                }
                else
                {
                    /* There is a comment there, remove it :) */
                    goto exit;
                }

                break;

            default:
                new[j++] = c;
                break;
        }
    }

exit:
    /* Make sure the char* is ended. */
    if (j == 0)
    {
        free(new);
        return NULL;
    } else {
        new[j] = '\0';
        return new;
    }
}

/*
 * Returns section name if line is a section.
 * Otherwise, returns NULL.
 */
char *
_conf_get_section(char *line, unsigned int linelen, unsigned int linenum)
{
    int i, j;
    char *section = NULL;
    char *errorMsg = NULL;
    int found_terminator = 0;

    for (i = 0, j = 0; i < linelen && !found_terminator; i++)
    {
        switch(line[i])
        {
            case '[':
                if (section)
                {
                    errorMsg = "Parse error near line %d: '[' followed by another '['\n";
                    goto error;
                }
                section = (char *)malloc(linelen+1-i);
                break;
            case ']':
                if (! section)
                {
                    errorMsg = "Parse error near line %d: ']' found, without '['.\n";
                    goto error;
                }
                found_terminator = 1;
                section[j] = 0;
                break;
            default:
                if (section)
                    section[j++] = line[i];
                break;
        }
    }

    if (! found_terminator)
    {
        errorMsg = "Parse error near line %d: Terminator ']' not found.\n";
        goto error;
    }

    return section;
error:
    if (section)
        free(section);
    if (errorMsg)
        _ERROR(errorMsg, linenum);
    return NULL;
}

/*
 * Parse the line and returns a confObj_t object,
 * or NULL if parse error.
 */
confObj_t *
_conf_parseLine(char *line, unsigned int linelen, unsigned int linenum)
{
    int i, j, k;
    int len;
    int quote, space;
    char *buf;
    char c;
    void *ptr;
    confObj_t *confObj;

    if ((line == NULL) || (*line == '\0'))
        return NULL;

    confObj = (confObj_t *)malloc(sizeof(confObj_t));
    confObj->var = NULL;
    confObj->val = NULL;

    buf = malloc(linelen+1);

    /* First step: getting variable */
    for (i = 0, quote = 0, space = 0, j = 0; i < linelen; i++)
    {
        c = line[i];

        switch (c)
        {
            case '\"':
                quote ^= 1;
                break;

            case '=':
                if (j == 0)
                {
                    _ERROR("Parse error near line %d: line should not start with '='.\n",
                        linenum);
                    free(confObj);
                    free(buf);
                    return NULL;
                }

                if (confObj->var != NULL)
                {
                    _ERROR("Parse error near line %d: two '=' detected.\n", linenum);
                    free(confObj->var);
                    free(confObj);
                    free(buf);
                    return NULL;
                }

                buf[j] = 0;
                len = j;
                j = 0;

                /* Strip ending spaces */
                for (k = len-1; (k > 0) && (buf[k] == ' '); k--);
                buf[k+1] = 0;

                confObj->var = strdup(buf);
                break;

            default:
                if ((c == ' ') && (space == 0))
                    space = 1;
                else if (c != ' ')
                    space = 0;

                if ((space == 1) && (quote == 0))
                    break; /* Discards */
                if ((space == 1) && (j == 0))
                    break; /* Discards */

                if (! isascii(c))
                {
                    _ERROR("Parse error near line %d: "
                        "variable/value must be *ASCII ONLY*\n",
                        linenum);
                    free(buf);
                    if (confObj->var)
                        free(confObj->var);
                    free(confObj);
                    return NULL;
                }
                buf[j++] = c;
        }
    }

    if ((j == 0) || (confObj->var == NULL))
    {
        /* _ERROR("Parse error near line %d: Variable not found.\n", linenum); */
        free(confObj);
        free(buf);
        return NULL;
    }

    buf[j] = 0;
    len = j;

    /* Ugly strip */
    for (k = len-1; (k > 0) && (buf[k] == ' '); k--);
    buf[k+1] = 0;
    len = k;

    /* Backup */
    ptr = buf;

    for (k = 0; (k < len) && (buf[k] == ' '); k++) buf++;

    if (! *buf)
    {
        _ERROR("Parse error near line %d: Value not found.\n", linenum);
        free(ptr);
        free(confObj->var);
        free(confObj);
        return NULL;
    }

    confObj->val = strdup(buf);

    /* Some cleanup */
    free(ptr);

    return confObj;
}

/* Private stuff */
config_t *
_conf_setValue(config_t *start,
    confObj_t *confObj,
    const char *section)
{
    config_t *work;
    config_t *last;

    if (start == NULL)
    {
        _ERROR("Error in setValue: config start is NULL.\n");
        return NULL;
    }

    if (section == NULL)
    {
        _ERROR("Error in setValue: section is NULL.\n");
        return NULL;
    }

    if (start->conf == NULL)
    {
        start->conf     = confObj;
        start->section  = strdup(section);
        start->next     = NULL; /* Useless, but safe :) */
    } else {
        work = (config_t *)malloc(sizeof(config_t));
        work->next    = NULL;
        work->section = strdup(section);
        work->conf    = confObj;

        last = start;
        while (last->next != NULL)
            last = last->next;

        last->next    = work;
    }

    return start;
}

/* Public stuff :) */
void
conf_setValue( config_t **start,
    const char *var,
    const char *val,
    const char *section )
{
    confObj_t *obj;

    if ((var == NULL) || (val == NULL))
    {
        _WARNING("var = NULL or val = NULL\n");
        return;
    }

    if ((*start) == NULL)
    {
        (*start) = (config_t *)malloc(sizeof(config_t));
        (*start)->next      = NULL;
        (*start)->section   = NULL;
        (*start)->conf      = NULL;
    }

    obj = (confObj_t *)malloc(sizeof(confObj_t));
    obj->var = strdup(var);
    obj->val = strdup(val);

    (*start) = _conf_setValue( *start, obj, section );
}

/*
 * This function will walk thru config_t *start
 * and affect dest to the value of var in config file.
 */
config_t *
_conf_getValue(config_t *start,
    const char *section,
    const char *var,
    char **dest )
{
    while (start != NULL)
    {
        if ((start->conf == NULL) ||
                (start->conf->var == NULL) ||
                (! mystr_eq(start->section, section)))
            goto next;

        if (mystr_eq(start->conf->var, var) == 1)
        {
            *dest = strdup(start->conf->val);
            return start->next;
        }
next:
        start = start->next;
    }

    *dest = NULL;
    return NULL;
}

void
_conf_printConfig(config_t *start)
{
    config_t *tmp = start;
    while (tmp != NULL)
    {
        if (tmp->conf)
            printf("section: %s, var: `%s' val: `%s'\n",
                tmp->section, tmp->conf->var, tmp->conf->val);
        tmp = tmp->next;
    }
}

void
_conf_freeConfig(config_t *start)
{
    config_t *old;

    while (start != NULL)
    {
        if (start->conf != NULL)
        {
            if (start->conf->var != NULL)
                free(start->conf->var);
            if (start->conf->val != NULL)
                free(start->conf->val);

            free(start->conf);
        }

        if (start->section != NULL)
            free(start->section);

        old = start;
        start = start->next;
        free(old);
    }
}

void
_conf_set_str_from_conf(config_t *config,
    const char *section,
    const char *type,
    char **value,
    const char *def,
    const char *errMsg,
    int exit_n)
{
    _conf_getValue(config, section, type, value);

    if (*value == NULL)
    {
        if (errMsg)
            fprintf(stderr, "%s", errMsg);
        if (def != NULL)
            *value = strdup(def);
        if (exit_n > 0)
        {
            fprintf(stderr, "Will quit with exit code %d\n", exit_n);
            exit(exit_n);
        }
    }
}

void
_conf_set_int_from_conf(config_t *config,
    const char *section,
    const char *type,
    int *value,
    int def,
    const char *errMsg,
    int exit_n)
{
    char *tmp;
    _conf_getValue(config, section, type, &tmp);

    if ( tmp == NULL )
    {
        if (errMsg)
            fprintf(stderr, "%s", errMsg);
        *value = def;
        if (exit_n > 0)
        {
            fprintf(stderr, "Will quit with exit code %d\n", exit_n);
            exit(exit_n);
        }
    } else {
        *value = atoi(tmp);
        free(tmp);
    }
}

void
_conf_set_bool_from_conf(config_t *config,
    const char *section,
    const char *type,
    int *value,
    int def,
    const char *errMsg,
    int exit_n)
{
    char *tmp;
    _conf_getValue(config, section, type, &tmp);

    if ( tmp == NULL )
    {
        if (errMsg)
            fprintf(stderr, "%s", errMsg);
        *value = def;
        if (exit_n > 0)
        {
            fprintf(stderr, "Will quit with exit code %d\n", exit_n);
            exit(exit_n);
        }
    } else {
        *value = atoi(tmp);
        if ( (*value) != 1)
            *value = 0;
        free(tmp);
    }
}
