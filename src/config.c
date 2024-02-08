#include <scfg.h>
#include <string.h>
#include <limits.h>

#include "anthywl.h"
#include "config.h"

char const anthywl_default_config[] =
#include "default_config.inc"
;

static void anthywl_config_load_bindings(struct anthywl_config *config,
    struct scfg_block *block, struct wl_array *bindings)
{
    for (size_t i = 0; i < block->directives_len; i++) {
        struct scfg_directive *directive = &block->directives[i];
        if (directive->params_len != 1) {
            fprintf(stderr, "line %d: invalid number of parameters "
                "for bindings directive, ignoring\n", directive->lineno);
            return;
        }

        char *s = directive->name, *p;
        struct anthywl_binding binding = { 0 };
        while ((p = strchr(s, '+'))) {
            *p = 0;
            if (strcmp(s, "Shift") == 0)
                binding.modifiers |= ANTHYWL_SHIFT;
            else if (strcmp(s, "Lock") == 0)
                binding.modifiers |= ANTHYWL_CAPS;
            else if (strcmp(s, "Ctrl") == 0 || strcmp(s, "Control") == 0)
                binding.modifiers |= ANTHYWL_CTRL;
            else if (strcmp(s, "Mod1") == 0 || strcmp(s, "Alt") == 0)
                binding.modifiers |= ANTHYWL_ALT;
            else if (strcmp(s, "Mod2") == 0)
                binding.modifiers |= ANTHYWL_NUM;
            else if (strcmp(s, "Mod3") == 0)
                binding.modifiers |= ANTHYWL_MOD3;
            else if (strcmp(s, "Mod4") == 0)
                binding.modifiers |= ANTHYWL_LOGO;
            else if (strcmp(s, "Mod5") == 0)
                binding.modifiers |= ANTHYWL_MOD5;
            else {
                fprintf(stderr,
                    "line %d: invalid modifier %s, binding ignored\n",
                    directive->lineno, s);
                return;
            }
            s = p + 1;
        }
        binding.keysym = xkb_keysym_from_name(s,  XKB_KEYSYM_CASE_INSENSITIVE);
        if (binding.keysym == XKB_KEY_NoSymbol) {
            fprintf(stderr, "line %d: invalid key %s, binding ignored\n",
                directive->lineno, s);
            continue;
        }
        binding.action = anthywl_action_from_string(directive->params[0]);
        *(struct anthywl_binding *)wl_array_add(bindings, sizeof binding) =
            binding;
    }
    qsort(bindings->data, bindings->size / sizeof(struct anthywl_binding),
        sizeof(struct anthywl_binding), anthywl_binding_compare);
}

static void anthywl_config_load_root(struct anthywl_config *config,
    struct scfg_block *root)
{
    for (size_t i = 0; i < root->directives_len; i++) {
        struct scfg_directive *directive = &root->directives[i];
        if (strcmp(directive->name, "active-at-startup") == 0) {
            if (directive->params_len != 0)
                fprintf(stderr,
                    "line %d: too many arguments to active-at-startup\n",
                    directive->lineno);
            else
                config->active_at_startup = true;
        } else if (strcmp(directive->name, "emulate-im-popups") == 0) {
            if (directive->params_len != 0)
                fprintf(stderr,
                    "line %d: too many arguments to emulate-im-popups\n",
                    directive->lineno);
            else
                config->emulate_im_popups = true;
        } else if (strcmp(directive->name, "global-bindings") == 0) {
            anthywl_config_load_bindings(
                config, &directive->children, &config->global_bindings);
        } else if (strcmp(directive->name, "composing-bindings") == 0) {
            anthywl_config_load_bindings(
                config, &directive->children, &config->composing_bindings);
        } else if (strcmp(directive->name, "selecting-bindings") == 0) {
            anthywl_config_load_bindings(
                config, &directive->children, &config->selecting_bindings);
        } else {
            fprintf(stderr, "line %d: unknown section '%s'\n",
                directive->lineno, directive->name);
        }
    }
}

void anthywl_config_init(struct anthywl_config *config) {
    wl_array_init(&config->global_bindings);
    wl_array_init(&config->composing_bindings);
    wl_array_init(&config->selecting_bindings);
}

void anthywl_config_finish(struct anthywl_config *config) {
    wl_array_release(&config->selecting_bindings);
    wl_array_release(&config->composing_bindings);
    wl_array_release(&config->global_bindings);
}

bool anthywl_config_load(struct anthywl_config *config) {
    char path[PATH_MAX];
    char const *prefix;
    if ((prefix = getenv("XDG_CONFIG_HOME"))) {
        snprintf(path, sizeof path, "%s/anthywl/config", prefix);
    } else if ((prefix = getenv("HOME"))) {
        snprintf(path, sizeof path, "%s/.config/anthywl/config", prefix);
    } else {
        fprintf(stderr, "cannot find config file\n");
        return false;
    }

    FILE *f = fopen(path, "r");
    if (f == NULL)
        f = fmemopen((char *)anthywl_default_config,
        sizeof anthywl_default_config, "r");

    if (f == NULL) {
        perror("failed to open default config file");
        return false;
    }

    struct scfg_block root;
    if (scfg_parse_file(&root, f))
        goto close;
    anthywl_config_load_root(config, &root);
    scfg_block_finish(&root);
close:
    fclose(f);
    return true;
}

