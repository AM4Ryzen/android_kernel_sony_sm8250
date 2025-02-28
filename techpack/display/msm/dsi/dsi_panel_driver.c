/* Copyright (c) 2015-2019, The Linux Foundation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/sysfs.h>
#include <linux/device.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/notifier.h>
#include <linux/export.h>
#include <linux/drm_notify.h>
#include "dsi_panel_driver.h"
#include "dsi_panel.h"
#include "dsi_display.h"

static BLOCKING_NOTIFIER_HEAD(drm_notifier_list);
struct device virtdev;

static char *res_buf;
static int buf_sz;
#define DSI_BUF_SIZE 1024
#define TMP_BUF_SZ 128
#define MAX_WRITE_DATA 100

#define BR_MAX_FIGURE	9
#define AREA_COUNT_MAX	9999999

int drm_register_client(struct notifier_block *nb)
{
	return blocking_notifier_chain_register(&drm_notifier_list, nb);
}
EXPORT_SYMBOL(drm_register_client);

int drm_unregister_client(struct notifier_block *nb)
{
	return blocking_notifier_chain_unregister(&drm_notifier_list, nb);
}
EXPORT_SYMBOL(drm_unregister_client);

int drm_notifier_call_chain(unsigned long val, void *v)
{
	return blocking_notifier_call_chain(&drm_notifier_list, val, v);
}
EXPORT_SYMBOL_GPL(drm_notifier_call_chain);

static int dsi_panel_driver_vreg_name_to_config(
		struct dsi_regulator_info *regs,
		struct dsi_vreg *config,
		char *name)
{
	int num_vreg = regs->count;
	int i = 0;
	int valid = -EINVAL;

	for (i = 0; i < num_vreg; i++) {
		if (!strcmp(name, regs->vregs[i].vreg_name)) {
			*config = regs->vregs[i];
			valid = 0;
			break;
		}
	}

	return valid;
}

static int dsi_panel_driver_vreg_ctrl(
		struct dsi_regulator_info *regs, char *vreg_name, bool enable)
{
	struct dsi_vreg vreg;
	int num_of_v = 0;
	int rc = 0;
	int valid = 0;

	valid = dsi_panel_driver_vreg_name_to_config(
			regs, &vreg, vreg_name);

	if (!valid) {
		if (enable) {
			pr_debug("%s: vreg on, name:%s\n", __func__,
							vreg.vreg_name);

			if (vreg.pre_on_sleep)
				usleep_range(vreg.pre_on_sleep * 1000,
						vreg.pre_on_sleep * 1000 + 100);

			rc = regulator_set_load(vreg.vreg,
						vreg.enable_load);
			if (rc < 0) {
				pr_err("Setting optimum mode failed for %s\n",
				       vreg.vreg_name);
				goto error;
			}
			num_of_v = regulator_count_voltages(vreg.vreg);
			if (num_of_v > 0) {
				rc = regulator_set_voltage(vreg.vreg,
							   vreg.min_voltage,
							   vreg.max_voltage);
				if (rc) {
					pr_err("Set voltage(%s) fail, rc=%d\n",
						 vreg.vreg_name, rc);
					goto error;
				}
			}

			rc = regulator_enable(vreg.vreg);
			if (rc) {
				pr_err("enable failed for %s, rc=%d\n",
				       vreg.vreg_name, rc);
				goto error;
			}

			if (vreg.post_on_sleep)
				usleep_range(vreg.post_on_sleep * 1000,
						vreg.post_on_sleep * 1000 + 100);
		} else {
			pr_debug("%s: vreg off, name:%s\n", __func__,
							vreg.vreg_name);

			if (vreg.pre_off_sleep)
				usleep_range(vreg.pre_off_sleep * 1000,
						vreg.pre_off_sleep * 1000 + 100);

			(void)regulator_set_load(vreg.vreg,
						vreg.disable_load);
			(void)regulator_disable(vreg.vreg);

			if (vreg.post_off_sleep)
				usleep_range(vreg.post_off_sleep * 1000,
						vreg.post_off_sleep * 1000 + 100);
		}

	}

	return 0;
error:
	return rc;
}

static int dsi_panel_driver_vreg_is_enable(
		struct dsi_regulator_info *regs, char *vreg_name)
{
	struct dsi_vreg vreg;
	int rc = 0;
	int valid = 0;

	valid = dsi_panel_driver_vreg_name_to_config(
			regs, &vreg, vreg_name);

	if (!valid) {
		rc =  regulator_is_enabled(vreg.vreg);
		pr_debug("%s: %s is enable = %d\n", __func__,
						vreg.vreg_name, rc);
	}

	return rc;
}

int dsi_panel_driver_pinctrl_init(struct dsi_panel *panel)
{
	panel->pinctrl.touch_state_active
		= pinctrl_lookup_state(panel->pinctrl.pinctrl,
				SDE_PINCTRL_STATE_TOUCH_ACTIVE);
	if (IS_ERR_OR_NULL(panel->pinctrl.touch_state_active))
		pr_warn("%s: can not get touch active pinstate\n", __func__);

	panel->pinctrl.touch_state_suspend
		= pinctrl_lookup_state(panel->pinctrl.pinctrl,
				SDE_PINCTRL_STATE_TOUCH_SUSPEND);
	if (IS_ERR_OR_NULL(panel->pinctrl.touch_state_suspend))
		pr_warn("%s: can not get touch suspend pinstate\n", __func__);

	return 0;
}

static int dsi_panel_driver_touch_pinctrl_set_state(
	struct dsi_panel *panel,
	bool active)
{
	struct panel_specific_pdata *spec_pdata = NULL;
	struct pinctrl_state *pin_state;
	int rc = -EFAULT;

	if (!panel) {
		pr_err("%s: Invalid input data\n", __func__);
		return -EINVAL;
	}

	spec_pdata = panel->spec_pdata;

	pin_state = active ? panel->pinctrl.touch_state_active
				: panel->pinctrl.touch_state_suspend;

	if (!IS_ERR_OR_NULL(pin_state)) {
		rc = pinctrl_select_state(panel->pinctrl.pinctrl,
				pin_state);
		if (rc)
			pr_err("%s: can not set %s pins\n", __func__,
			       active ? SDE_PINCTRL_STATE_TOUCH_ACTIVE
			       : SDE_PINCTRL_STATE_TOUCH_SUSPEND);
	} else {
		pr_err("%s: invalid '%s' pinstate\n", __func__,
		       active ? SDE_PINCTRL_STATE_TOUCH_ACTIVE
		       : SDE_PINCTRL_STATE_TOUCH_SUSPEND);
	}

	return rc;
}

int dsi_panel_driver_gpio_request(struct dsi_panel *panel)
{
	struct panel_specific_pdata *spec_pdata = NULL;
	int rc;

	if (!panel) {
		pr_err("%s: Invalid input panel\n", __func__);
		return -EINVAL;
	}
	spec_pdata = panel->spec_pdata;

	if (gpio_is_valid(spec_pdata->reset_touch_gpio)) {
		rc = gpio_request(spec_pdata->reset_touch_gpio,
							"reset_touch_gpio");
		if (rc) {
			pr_err("request for reset_gpio failed, rc=%d\n", rc);
			goto error_release_touch_reset;
		}
	}

	if (gpio_is_valid(spec_pdata->disp_err_fg_gpio)) {
		rc = gpio_request(spec_pdata->disp_err_fg_gpio, "disp_err_fg_gpio");
		if (rc != 0) {
			pr_err("request disp err fg gpio failed, rc=%d\n", rc);
		}
	}
	goto error;
error_release_touch_reset:
	if (gpio_is_valid(spec_pdata->reset_touch_gpio))
		gpio_free(spec_pdata->reset_touch_gpio);

error:
	return rc;
}

int dsi_panel_driver_gpio_release(struct dsi_panel *panel)
{
	struct panel_specific_pdata *spec_pdata = NULL;
	int rc = 0;

	if (!panel) {
		pr_err("%s: Invalid input panel\n", __func__);
		return -EINVAL;
	}
	spec_pdata = panel->spec_pdata;
	if (gpio_is_valid(spec_pdata->reset_touch_gpio))
		gpio_free(spec_pdata->reset_touch_gpio);

	return rc;
}

int dsi_panel_driver_touch_reset(struct dsi_panel *panel)
{
	struct panel_specific_pdata *spec_pdata = NULL;
	int rc, i;

	if (!panel) {
		pr_err("%s: Invalid input panel\n", __func__);
		return -EINVAL;
	}
	spec_pdata = panel->spec_pdata;

	if (spec_pdata->count_touch) {
		rc = gpio_direction_output(spec_pdata->reset_touch_gpio,
			spec_pdata->sequence_touch[0].level);
		if (rc) {
			pr_err("unable to set dir for rst touch gpio rc=%d\n", rc);
			goto exit;
		}
	}
	for (i = 0; i < spec_pdata->count_touch; i++) {
		gpio_set_value(spec_pdata->reset_touch_gpio,
			       spec_pdata->sequence_touch[i].level);

		if (spec_pdata->sequence_touch[i].sleep_ms)
			usleep_range(
				spec_pdata->sequence_touch[i].sleep_ms * 1000,
				spec_pdata->sequence_touch[i].sleep_ms * 1000);
	}

exit:
	return rc;
}

int dsi_panel_driver_touch_reset_ctrl(struct dsi_panel *panel, bool en)
{
	struct panel_specific_pdata *spec_pdata = NULL;
	int rc = 0;

	if (!panel) {
		pr_err("%s: Invalid input panel\n", __func__);
		return -EINVAL;
	}
	spec_pdata = panel->spec_pdata;

	gpio_set_value(spec_pdata->reset_touch_gpio, 0);
	usleep_range(spec_pdata->touch_reset_off * 1000,
			spec_pdata->touch_reset_off * 1000);
	gpio_set_value(spec_pdata->reset_touch_gpio, 1);

	return rc;
}

int dsi_panel_driver_reset_panel(struct dsi_panel *panel, bool en)
{
	struct dsi_panel_reset_config *r_config = &panel->reset_config;
	struct panel_specific_pdata *spec_pdata = NULL;
	struct dsi_reset_cfg *seq = NULL;
	int i;
	int rc = 0;

	if (!panel) {
		pr_err("%s: Invalid input panel\n", __func__);
		return -EINVAL;
	}
	spec_pdata = panel->spec_pdata;

	if (!gpio_is_valid(panel->reset_config.reset_gpio)) {
		pr_debug("%s:%d, panel reset line not configured\n",
			   __func__, __LINE__);
		goto exit;
	}

	pr_debug("%s: enable = %d\n", __func__, en);

	if (en)
		seq = &spec_pdata->on_seq;
	else
		seq = &spec_pdata->off_seq;

	if (seq->count) {
		pr_debug("%s: first level=%d\n",
				__func__, seq->seq[0].level);

		rc = gpio_direction_output(r_config->reset_gpio,
			seq->seq[0].level);
		if (rc) {
			pr_err("unable to set dir for rst gpio rc=%d\n", rc);
			goto exit;
		}
	}

	for (i = 0; i < seq->count; i++) {
		gpio_set_value(r_config->reset_gpio,
			       seq->seq[i].level);

		if (r_config->sequence[i].sleep_ms)
			usleep_range(seq->seq[i].sleep_ms * 1000,
				     seq->seq[i].sleep_ms * 1000);

		pr_debug("%s: level=%d, wait=%dms\n", __func__,
				seq->seq[i].level, seq->seq[i].sleep_ms);
	}

exit:
	return rc;
}

static void dsi_panel_driver_power_off_ctrl(void)
{
	struct incell_ctrl *incell = incell_get_info();
	incell_pw_seq p_seq = POWER_EXECUTE;

	switch (incell->state) {
	case INCELL_S100:
	case INCELL_S110:
		p_seq = POWER_SKIP;
		break;
	case INCELL_S101:
		p_seq = POWER_EXECUTE;
		break;
	case INCELL_S111:
		pr_notice("%s: Power on keep in lock state\n", __func__);
		p_seq = POWER_SKIP;
		break;
	case INCELL_S001:
		pr_notice("%s: Power off by unlock\n", __func__);
		p_seq = POWER_EXECUTE;
		break;
	default:
		pr_err("%s: Already power off\n", __func__);
		p_seq = POWER_SKIP;
		break;
	}

	incell->state &= INCELL_SYSTEM_STATE_OFF;

	incell->seq = p_seq;
}

static void dsi_panel_driver_power_on_ctrl(void)
{
	struct incell_ctrl *incell = incell_get_info();
	incell_pw_seq p_seq = POWER_EXECUTE;

	switch (incell->state) {
	case INCELL_S000:
	case INCELL_S010:
		p_seq = POWER_EXECUTE;
		break;
	case INCELL_S001:
	case INCELL_S011:
		p_seq = POWER_SKIP;
		break;
	default:
		pr_err("%s: Already power on\n", __func__);
		p_seq = POWER_SKIP;
		break;
	}

	incell->state |= INCELL_SYSTEM_STATE_ON;

	incell->seq = p_seq;
}

int dsi_panel_driver_power_off(struct dsi_panel *panel)
{
	struct panel_specific_pdata *spec_pdata = NULL;
	struct incell_ctrl *incell = incell_get_info();
	int rc = 0;

	dsi_panel_driver_power_off_ctrl();
	if (incell->seq == POWER_SKIP) {
		pr_notice("%s: Power off skip\n", __func__);
		return rc;
	}

	if (!panel) {
		pr_err("%s: Invalid input panel\n", __func__);
		return -EINVAL;
	}
	spec_pdata = panel->spec_pdata;

	if (spec_pdata->aod_mode == 1)
		spec_pdata->aod_mode = 0;

	if (spec_pdata->opec_mode == 1)
		spec_pdata->opec_mode = 0;

	if (spec_pdata->lp11_off) {
		usleep_range(spec_pdata->lp11_off * 1000,
				spec_pdata->lp11_off * 1000 + 100);
	}

	rc = dsi_panel_driver_reset_panel(panel, 0);
	if (rc)
		pr_err("%s: Panel reset failed. rc=%d\n", __func__, rc);

	if (!spec_pdata->pre_sod_mode) {
		if (gpio_is_valid(spec_pdata->reset_touch_gpio)) {
			gpio_set_value(spec_pdata->reset_touch_gpio, 0);
		} else {
			pr_err("%s: Touch reset failed.\n", __func__);
		}
	}

	return rc;
}

int dsi_panel_driver_post_power_off(struct dsi_panel *panel)
{
	struct panel_specific_pdata *spec_pdata = NULL;
	struct incell_ctrl *incell = incell_get_info();
	int rc = 0;
	struct drm_ext_event event;
	int blank = DRM_BLANK_POWERDOWN;
	event.data = &blank;

	if (incell->seq == POWER_SKIP) {
		pr_notice("%s: Post power off skip\n", __func__);
		drm_notifier_call_chain(DRM_EXT_EVENT_AFTER_BLANK, &event);
		return rc;
	}

	if (!panel) {
		pr_err("%s: Invalid input panel\n", __func__);
		return -EINVAL;
	}
	spec_pdata = panel->spec_pdata;

	if (!spec_pdata->pre_sod_mode) {
		dsi_panel_driver_touch_pinctrl_set_state(panel, false);

		rc = dsi_panel_driver_vreg_ctrl(&panel->power_info, "ts_vddh", false);
		if (rc)
			pr_err("%s: failed to disable ts_vddh, rc=%d\n", __func__, rc);

		if (spec_pdata->touch_vddh_off) {
			usleep_range(spec_pdata->touch_vddh_off * 1000,
					spec_pdata->touch_vddh_off * 1000 + 100);
		}

		rc = dsi_panel_driver_vreg_ctrl(&panel->power_info, "ts_io", false);
		if (rc)
			pr_err("%s: failed to disable ts_io, rc=%d\n", __func__, rc);
	}

	rc = dsi_panel_driver_vreg_ctrl(&panel->power_info, "vddio", false);
	if (rc)
		pr_err("%s: failed to disable vddio, rc=%d\n", __func__, rc);

	rc = dsi_panel_driver_vreg_ctrl(&panel->power_info, "vci", false);
	if (rc)
		pr_err("%s: failed to disable vci, rc=%d\n", __func__, rc);

	if (spec_pdata->down_period) {
		usleep_range(spec_pdata->down_period * 1000,
				spec_pdata->down_period * 1000 + 100);
	}

	if (rc)
		pr_err("%s: failed to power off\n", __func__);
	else
		pr_notice("@@@@ panel power off @@@@\n");

	rc = dsi_panel_set_pinctrl_state(panel, false);
	if (rc)
		pr_err("%s: failed set pinctrl state, rc=%d\n", __func__, rc);

	incell->state &= INCELL_POWER_STATE_OFF;
	drm_notifier_call_chain(DRM_EXT_EVENT_AFTER_BLANK, &event);

	return rc;
}

int dsi_panel_driver_pre_power_on(struct dsi_panel *panel)
{
	struct panel_specific_pdata *spec_pdata = NULL;
	struct incell_ctrl *incell = incell_get_info();
	int rc = 0;
	int touch_power_state = 0;
	struct drm_ext_event event;
	int blank = DRM_BLANK_UNBLANK;
	event.data = &blank;

	dsi_panel_driver_power_on_ctrl();
	if (incell->seq == POWER_SKIP) {
		pr_notice("%s: Pre power on skip\n", __func__);
		drm_notifier_call_chain(DRM_EXT_EVENT_BEFORE_BLANK, &event);
		return rc;
	}

	spec_pdata = panel->spec_pdata;

	drm_notifier_call_chain(DRM_EXT_EVENT_BEFORE_BLANK, &event);

	rc = dsi_panel_driver_vreg_ctrl(&panel->power_info, "vddio", true);
	if (rc) {
		pr_err("%s: failed to enable vddio, rc=%d\n", __func__, rc);
		goto exit;
	}

	rc = dsi_panel_driver_vreg_ctrl(&panel->power_info, "vci", true);
	if (rc) {
		pr_err("%s: failed to enable vci, rc=%d\n", __func__, rc);
		goto exit;
	}

	touch_power_state = dsi_panel_driver_vreg_is_enable(
						&panel->power_info, "ts_io");
	if (!touch_power_state) {
		rc = dsi_panel_driver_vreg_ctrl(&panel->power_info, "ts_io", true);
		if (rc) {
			pr_err("%s: failed to enable ts_io, rc=%d\n", __func__, rc);
			goto exit;
		}
	}

	touch_power_state = dsi_panel_driver_vreg_is_enable(
						&panel->power_info, "ts_vddh");
	if (!touch_power_state) {
		rc = dsi_panel_driver_vreg_ctrl(&panel->power_info, "ts_vddh", true);
		if (rc) {
			pr_err("%s: failed to enable ts_vddh, rc=%d\n", __func__, rc);
			goto exit;
		}
	}

	if (spec_pdata->lp11_on) {
		usleep_range(spec_pdata->lp11_on * 1000,
				spec_pdata->lp11_on * 1000 + 100);
	}

	dsi_panel_driver_touch_pinctrl_set_state(panel, true);

exit:
	return rc;
}

int dsi_panel_driver_power_on(struct dsi_panel *panel)
{
	struct panel_specific_pdata *spec_pdata = NULL;
	struct incell_ctrl *incell = incell_get_info();
	int rc = 0;

	if (incell->seq == POWER_SKIP) {
		pr_notice("%s: Power on skip\n", __func__);
		return rc;
	}

	if (!panel) {
		pr_err("%s: Invalid input panel\n", __func__);
		return -EINVAL;
	}
	spec_pdata = panel->spec_pdata;

	rc = dsi_panel_driver_touch_reset(panel);
	if (rc) {
		pr_err("%s: failed to reset touch panel, rc=%d\n", __func__, rc);
		goto error_disable_vregs;
	}

	rc = dsi_panel_set_pinctrl_state(panel, true);
	if (rc) {
		pr_err("%s: failed to set pinctrl, rc=%d\n", __func__, rc);
		goto error_disable_vregs;
	}

	if (spec_pdata->panel_reset_on) {
		usleep_range(spec_pdata->panel_reset_on * 1000,
				spec_pdata->panel_reset_on * 1000 + 100);
	}

	rc = dsi_panel_reset(panel);
	if (rc) {
		pr_err("%s: failed to reset panel, rc=%d\n", __func__, rc);
		goto error_disable_gpio;
	}

	pr_notice("@@@@ panel power on @@@@\n");

	incell->state |= INCELL_POWER_STATE_ON;

	goto exit;

error_disable_gpio:
	if (gpio_is_valid(panel->bl_config.en_gpio))
		gpio_set_value(panel->bl_config.en_gpio, 0);

	(void)dsi_panel_set_pinctrl_state(panel, false);

error_disable_vregs:
	(void)dsi_pwr_enable_regulator(&panel->power_info, false);

	pr_err("%s: failed to power on\n", __func__);
exit:
	return rc;
}

static int dsi_panel_driver_parse_reset_touch_sequence(struct dsi_panel *panel)
{
	int rc = 0;
	int i;
	u32 length = 0;
	u32 count = 0;
	u32 size = 0;
	u32 *arr_32 = NULL;
	const u32 *arr;
	struct dsi_parser_utils *utils = &panel->utils;
	struct dsi_reset_seq *seq;
	struct panel_specific_pdata *spec_pdata = NULL;

	if (!panel) {
		pr_err("%s: Invalid input panel\n", __func__);
		return -EINVAL;
	}
	spec_pdata = panel->spec_pdata;

	arr = utils->get_property(utils->data,
			"somc,mdss-dsi-touch-reset-sequence", &length);
	if (!arr) {
		pr_err("[%s] dsi-reset-sequence not found\n", panel->name);
		rc = -EINVAL;
		goto error;
	}
	if (length & 0x1) {
		pr_err("[%s] syntax error for dsi-touch-reset-sequence\n",
		       panel->name);
		rc = -EINVAL;
		goto error;
	}

	pr_err("RESET SEQ LENGTH = %d\n", length);
	length = length / sizeof(u32);

	size = length * sizeof(u32);

	arr_32 = kzalloc(size, GFP_KERNEL);
	if (!arr_32) {
		rc = -ENOMEM;
		goto error;
	}

	rc = utils->read_u32_array(utils->data, "somc,mdss-dsi-touch-reset-sequence",
					arr_32, length);
	if (rc) {
		pr_err("[%s] cannot read dsi-touch-reset-sequence\n", panel->name);
		goto error_free_arr_32;
	}

	count = length / 2;
	size = count * sizeof(*seq);
	seq = kzalloc(size, GFP_KERNEL);
	if (!seq) {
		rc = -ENOMEM;
		goto error_free_arr_32;
	}

	spec_pdata->sequence_touch = seq;
	spec_pdata->count_touch = count;

	for (i = 0; i < length; i += 2) {
		seq->level = arr_32[i];
		seq->sleep_ms = arr_32[i + 1];
		seq++;
	}

error_free_arr_32:
	kfree(arr_32);
error:
	return rc;
}

static int dsi_property_read_u32_var(struct device_node *np,
		char *name, struct dsi_reset_cfg *out)
{
	int rc = 0;
	int i;
	u32 length = 0;
	u32 count = 0;
	u32 size = 0;
	u32 *arr_32 = NULL;
	const u32 *arr;
	struct dsi_reset_seq *seq;
	struct property *prop = of_find_property(np, name, NULL);

	if (!prop) {
		pr_debug("%s:%d, unable to read %s", __func__, __LINE__, name);
		return -EINVAL;
	}

	arr = of_get_property(np, name, &length);
	if (length & 0x1) {
		pr_err("%s: syntax error for %s\n", __func__, name);
		rc = -EINVAL;
		goto error;
	}

	pr_debug("RESET SEQ LENGTH = %d\n", length);
	length = length / sizeof(u32);
	size = length * sizeof(u32);

	arr_32 = kzalloc(size, GFP_KERNEL);
	if (!arr_32) {
		rc = -ENOMEM;
		goto error;
	}

	rc = of_property_read_u32_array(np, name, arr_32, length);
	if (rc) {
		pr_err("%s: cannot read %s\n", __func__, name);
		goto error_free_arr_32;
	}

	count = length / 2;
	size = count * sizeof(*seq);
	seq = kzalloc(size, GFP_KERNEL);
	if (!seq) {
		rc = -ENOMEM;
		goto error_free_arr_32;
	}

	out->seq = seq;
	out->count = count;

	for (i = 0; i < length; i += 2) {
		seq->level = arr_32[i];
		seq->sleep_ms = arr_32[i + 1];
		seq++;
	}

error_free_arr_32:
	kfree(arr_32);
error:
	return rc;
}

int dsi_panel_driver_parse_dt(struct dsi_panel *panel,
					struct device_node *np)
{
	struct panel_specific_pdata *spec_pdata = NULL;
	struct dsi_regulator_info *power_info = NULL;
	u32 tmp = 0;
	int i = 0;
	int rc = 0;
	int valid = -EINVAL;

	if (!panel) {
		pr_err("%s: Invalid input panel\n", __func__);
		return -EINVAL;
	}
	spec_pdata = panel->spec_pdata;

	if (of_find_property(np, "somc,pw-off-rst-b-seq", NULL)) {
		spec_pdata->rst_b_seq = true;

		(void)dsi_property_read_u32_var(np,
			"somc,pw-off-rst-b-seq",
			&spec_pdata->off_seq);
	} else {
		(void)dsi_property_read_u32_var(np,
			"somc,pw-off-rst-seq",
			&spec_pdata->off_seq);
	}

	spec_pdata->on_seq.seq = panel->reset_config.sequence;
	spec_pdata->on_seq.count = panel->reset_config.count;

	power_info = &panel->power_info;

	for (i = 0; i < power_info->count; i++) {
		if (!strcmp("vddio", power_info->vregs[i].vreg_name)) {
			valid = 0;
			break;
		}
	}
	if (!valid) {
		rc = of_property_read_u32(np,
				"somc,pw-wait-after-on-vddio", &tmp);
		power_info->vregs[i].post_on_sleep = !rc ? tmp : 0;

		rc = of_property_read_u32(np,
				"somc,pw-wait-after-off-vddio", &tmp);
		power_info->vregs[i].post_off_sleep = !rc ? tmp : 0;
	} else {
		pr_notice("%s: vddio power info not registered\n", __func__);
	}

	panel->lp11_init = of_property_read_bool(np,
			"qcom,mdss-dsi-lp11-init");

	rc = of_property_read_u32(np, "somc,pw-wait-before-on-lp11", &tmp);
	spec_pdata->lp11_on = !rc ? tmp : 0;

	rc = of_property_read_u32(np, "somc,pw-wait-before-panel-reset", &tmp);
	spec_pdata->panel_reset_on = !rc ? tmp : 0;

	rc = of_property_read_u32(np, "somc,pw-wait-after-off-lp11", &tmp);
	spec_pdata->lp11_off = !rc ? tmp : 0;

	rc = of_property_read_u32(np, "somc,pw-wait-after-off-touch-vddh", &tmp);
	spec_pdata->touch_vddh_off = !rc ? tmp : 0;

	rc = of_property_read_u32(np, "somc,pw-wait-downperiod", &tmp);
	spec_pdata->down_period = !rc ? tmp : 0;

	rc = of_property_read_u32(np, "somc,pw-wait-after-off-touch-reset", &tmp);
	spec_pdata->touch_reset_off = !rc ? tmp : 0;

	rc = of_property_read_u32(np,
		"somc,area_count_table_size", &tmp);
	if (!rc) {
		spec_pdata->area_count_table_size = tmp;
	} else if (rc != -EINVAL) {
		pr_err("%s: Unable to read area_count_table_size\n", __func__);
		goto area_count_error;
	}

	if (spec_pdata->area_count_table_size > 0) {
		spec_pdata->area_count_table = kzalloc(sizeof(int) * spec_pdata->area_count_table_size, GFP_KERNEL);
		if (!spec_pdata->area_count_table) {
			pr_err("%s: area_count_table kzalloc error ", __func__);
			goto area_count_error;
		}

		spec_pdata->area_count = kzalloc(sizeof(u32) * spec_pdata->area_count_table_size, GFP_KERNEL);
		if (!spec_pdata->area_count) {
			kfree(spec_pdata->area_count_table);
			pr_err("%s: area_count kzalloc error ", __func__);
			goto area_count_error;
		}

		rc = of_property_read_u32_array(np, "somc,area_count_table",
			spec_pdata->area_count_table, spec_pdata->area_count_table_size);
		if (rc < 0) {
			kfree(spec_pdata->area_count_table);
			kfree(spec_pdata->area_count);
			pr_err("%s: Unable to read area_count_table\n", __func__);
			goto area_count_error;
		}
	}

	return 0;
area_count_error:
	spec_pdata->area_count_table_size = 0;

	return 0;
}

int dsi_panel_driver_parse_gpios(struct dsi_panel *panel)
{
	int rc = 0;
	struct panel_specific_pdata *spec_pdata = NULL;

	if (!panel) {
		pr_err("%s: Invalid input panel\n", __func__);
		return -EINVAL;
	}
	spec_pdata = panel->spec_pdata;

	spec_pdata->reset_touch_gpio = of_get_named_gpio(panel->panel_of_node,
					      "somc,platform-touch-reset-gpio",
					      0);
	if (!gpio_is_valid(spec_pdata->reset_touch_gpio)) {
		pr_err("%s: failed get reset touch gpio\n", __func__);
	}

	rc = dsi_panel_driver_parse_reset_touch_sequence(panel);
	if (rc) {
		pr_err("%s: failed to parse reset touch sequence, rc=%d\n",
		       __func__, rc);
		goto error;
	}

	spec_pdata->disp_err_fg_gpio = of_get_named_gpio(panel->panel_of_node,
						"somc,disp-err-flag-gpio", 0);
	if (!gpio_is_valid(spec_pdata->disp_err_fg_gpio))
		pr_err("%s: failed get disp error flag gpio\n", __func__);

error:
	return rc;
}

static void dsi_panel_driver_notify_resume(struct dsi_panel *panel)
{
	struct drm_ext_event event;
	int blank = DRM_BLANK_UNBLANK;
	event.data = &blank;

	drm_notifier_call_chain(DRM_EXT_EVENT_AFTER_BLANK, &event);
}

static void dsi_panel_driver_notify_suspend(struct dsi_panel *panel)
{
	struct drm_ext_event event;
	int blank = DRM_BLANK_POWERDOWN;
	event.data = &blank;

	drm_notifier_call_chain(DRM_EXT_EVENT_BEFORE_BLANK, &event);
}

void dsi_panel_driver_post_enable(struct dsi_panel *panel)
{
	panel->spec_pdata->display_onoff_state = true;

	dsi_panel_driver_oled_short_det_enable(panel->spec_pdata, SHORT_WORKER_PASSIVE);
	dsi_panel_driver_notify_resume(panel);
}

void dsi_panel_driver_pre_disable(struct dsi_panel *panel)
{
	dsi_panel_driver_oled_short_det_disable(panel->spec_pdata);
	dsi_panel_driver_notify_suspend(panel);
}

void dsi_panel_driver_disable(struct dsi_panel *panel)
{
	panel->spec_pdata->display_onoff_state = false;
}

bool dsi_panel_driver_is_power_on(unsigned char state)
{
	bool ret = false;

	if (state & INCELL_POWER_STATE_ON)
		ret = true;

	pr_debug("%s: In-Cell %s state\n", __func__, (ret ? "on" : "off"));

	return ret;
}

bool dsi_panel_driver_is_power_lock(unsigned char state)
{
	bool ret = false;

	if (state & INCELL_LOCK_STATE_ON)
		ret = true;

	pr_debug("%s: In-Cell I/F %s state\n", __func__,
		(ret ? "Lock" : "Unlock"));

	return ret;
}

static ssize_t dsi_panel_driver_id_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct dsi_display *display = dev_get_drvdata(dev);
	char const *id = display->panel->name ?
			display->panel->name : "default";

	return scnprintf(buf, PAGE_SIZE, "%s\n", id);
}

static ssize_t dsi_panel_pre_sod_mode_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	int mode;
	struct panel_specific_pdata *spec_pdata = NULL;
	struct dsi_display *display = dev_get_drvdata(dev);

	mutex_lock(&display->display_lock);
	spec_pdata = display->panel->spec_pdata;
	if (spec_pdata == NULL) {
		pr_err("%s: Invalid parameter\n", __func__);
		mutex_unlock(&display->display_lock);
		return -EINVAL;
	}

	if (sscanf(buf, "%d", &mode) < 0) {
		pr_err("sscanf failed to set mode. keep current mode=%d\n",
			spec_pdata->pre_sod_mode);
		mutex_unlock(&display->display_lock);
		return -EINVAL;
	}

	spec_pdata->pre_sod_mode = mode;
	pr_info("%s: sod mode setting %d\n", __func__, spec_pdata->pre_sod_mode);
	mutex_unlock(&display->display_lock);

	return count;
}

static ssize_t dsi_panel_pre_sod_mode_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct panel_specific_pdata *spec_pdata = NULL;
	struct dsi_display *display = dev_get_drvdata(dev);

	spec_pdata = display->panel->spec_pdata;
	if (spec_pdata == NULL) {
		pr_err("%s: Invalid parameter\n", __func__);
		return -EINVAL;
	}

	return scnprintf(buf, PAGE_SIZE, "pre_sod_mode = %u\n",
				spec_pdata->pre_sod_mode);
}

static ssize_t dsi_panel_driver_hbm_mode_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	struct dsi_display *display = dev_get_drvdata(dev);
	struct dsi_panel *panel = display->panel;
	int mode;

	mutex_lock(&display->display_lock);
	if (!panel->spec_pdata->display_onoff_state) {
		pr_err("%s: Display is off, can't set HBM status\n", __func__);
		goto hbm_error;
	}

	if (sscanf(buf, "%d", &mode) < 0) {
		pr_err("sscanf failed to set mode. keep current mode=%d\n",
			panel->spec_pdata->hbm_mode);
		goto hbm_error;
	}

	dsi_panel_set_hbm_mode(panel, mode);

	mutex_unlock(&display->display_lock);
	return count;

hbm_error:
	mutex_unlock(&display->display_lock);
	return -EINVAL;
}

static ssize_t dsi_panel_driver_hbm_mode_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct dsi_display *display = dev_get_drvdata(dev);

	return scnprintf(buf, PAGE_SIZE, "%u", display->panel->spec_pdata->hbm_mode);
}

static ssize_t dsi_panel_driver_flm2_mode_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	struct dsi_display *display = dev_get_drvdata(dev);
	struct dsi_panel *panel = display->panel;
	int mode;

	mutex_lock(&display->display_lock);
	if (!panel->spec_pdata->display_onoff_state) {
		pr_err("%s: Display is off, can't set FLM2 mode\n", __func__);
		goto flm_error;
	}

	if (sscanf(buf, "%d", &mode) < 0) {
		pr_err("sscanf failed to set mode. keep current mode\n");
		goto flm_error;
	}

	dsi_panel_set_flm2_mode(panel, mode);

	mutex_unlock(&display->display_lock);
	return count;

flm_error:
	mutex_unlock(&display->display_lock);
	return -EINVAL;
}

static ssize_t dsi_panel_driver_opec_mode_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	struct dsi_display *display = dev_get_drvdata(dev);
	struct dsi_panel *panel = display->panel;
	int mode;

	mutex_lock(&display->display_lock);
	if (!panel->spec_pdata->display_onoff_state) {
		pr_err("%s: Display is off, can't set OPEC status\n", __func__);
		goto opec_error;
	}

	if (sscanf(buf, "%d", &mode) < 0) {
		pr_err("sscanf failed to set mode. keep current mode=%d\n",
			panel->spec_pdata->opec_mode);
		goto opec_error;
	}

	dsi_panel_set_opec_mode(panel, mode);

	mutex_unlock(&display->display_lock);
	return count;

opec_error:
	mutex_unlock(&display->display_lock);
	return -EINVAL;
}

static ssize_t dsi_panel_driver_opec_mode_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct dsi_display *display = dev_get_drvdata(dev);

	return scnprintf(buf, PAGE_SIZE, "%u", display->panel->spec_pdata->opec_mode);
}

static void update_res_buf(char *string)
{
	res_buf = krealloc(res_buf, buf_sz + strnlen(string, TMP_BUF_SZ) + 1,
								GFP_KERNEL);
	if (!res_buf) {
		pr_err("%s: Failed to allocate buffer\n", __func__);
		return;
	}

	memcpy(res_buf + buf_sz, string, strnlen(string, TMP_BUF_SZ) + 1);
	buf_sz += strnlen(string, TMP_BUF_SZ); /* Exclude NULL termination */
}

static void reset_res_buf(void)
{
	kzfree(res_buf);
	res_buf = NULL;
	buf_sz = 0;
}

static int get_parameters(const char *p, u8 *par_buf, int par_buf_size,
								int *nbr_params)
{
	int ret = 0;

	while (true) {
		if (isspace(*p)) {
			p++;
		} else {
			if (sscanf(p, "%4hhx", &par_buf[*nbr_params]) == 1) {
				(*nbr_params)++;
				while (isxdigit(*p) || (*p == 'x'))
					p++;
			}
		}
		if (*nbr_params > par_buf_size) {
			update_res_buf("Too many parameters\n");
			ret = -EINVAL;
			goto exit;
		}
		if (iscntrl(*p))
			break;
	}
exit:
	return ret;
}

static int get_cmd_type(char *buf, enum dbg_cmd_type *cmd)
{
	int ret = 0;

	if (!strncmp(buf, "dcs", 3))
		*cmd = DCS;
	else if (!strncmp(buf, "gen", 3))
		*cmd = GEN;
	else
		ret = -EFAULT;
	return ret;
}

static void print_params(int dtype, u8 reg, int len, u8 *data)
{
	int i = 0;
	char tmp[TMP_BUF_SZ];

	switch (dtype) {
	case DTYPE_GEN_WRITE:
		update_res_buf("GEN_WRITE\n");
		break;
	case DTYPE_GEN_WRITE1:
		update_res_buf("GEN_WRITE1\n");
		break;
	case DTYPE_GEN_WRITE2:
		update_res_buf("GEN_WRITE2\n");
		break;
	case DTYPE_GEN_LWRITE:
		update_res_buf("GEN_LWRITE\n");
		break;
	case DTYPE_GEN_READ:
		update_res_buf("GEN_READ\n");
		break;
	case DTYPE_GEN_READ1:
		update_res_buf("GEN_READ1\n");
		break;
	case DTYPE_GEN_READ2:
		update_res_buf("GEN_READ2\n");
		break;
	case DTYPE_DCS_LWRITE:
		update_res_buf("DCS_LWRITE\n");
		break;
	case DTYPE_DCS_WRITE:
		update_res_buf("DCS_WRITE\n");
		break;
	case DTYPE_DCS_WRITE1:
		update_res_buf("DCS_WRITE1\n");
		break;
	case DTYPE_DCS_READ:
		update_res_buf("DCS_READ\n");
		break;
	default:
		snprintf(tmp, sizeof(tmp), "Unknown dtype = 0x%x\n", dtype);
		update_res_buf(tmp);
	}

	if (len > 0) {
		snprintf(tmp, sizeof(tmp), "reg=0x%.2X\n", reg);
		update_res_buf(tmp);
		snprintf(tmp, sizeof(tmp), "len=%d\n", len);
		update_res_buf(tmp);
		for (i = 0; i < len; i++) {
			snprintf(tmp, sizeof(tmp), "data[%d]=0x%.2X\n", i,
								data[i]);
			update_res_buf(tmp);
		}
	} else {
		update_res_buf("Something went wrong, length is zero.\n");
		snprintf(tmp, sizeof(tmp),
				"reg=0x%.2X, len=%d, data[0]=0x%.2X\n",
				reg, len, data[0]);
		update_res_buf(tmp);
	}
}

static ssize_t dsi_panel_driver_reg_write_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	struct dsi_display *display = dev_get_drvdata(dev);
	const char *p;
	enum dbg_cmd_type cmd;
	u8 data[MAX_WRITE_DATA];
	int i = 0;
	int ret;
	struct dsi_cmd_desc dsi;

	if (!display->panel->spec_pdata->display_onoff_state) {
		pr_err("%s: panel is NOT on\n", __func__);
		goto fail_free_all;
	}

	reset_res_buf();

	ret = get_cmd_type((char *)buf, &cmd);
	if (ret) {
		update_res_buf("Write - unknown type\n");
		goto fail_free_all;
	}

	p = buf;
	p = p+4;

	/* Get first param, Register */
	if (sscanf(p, "%4hhx", &data[0]) != 1) {
		update_res_buf("Write - parameter error\n");
		goto fail_free_all;
	}
	i++;

	while (isxdigit(*p) || (*p == 'x'))
		p++;

	ret = get_parameters(p, data, ARRAY_SIZE(data) - 1, &i);
	if (ret)
		goto fail_free_all;

	if (cmd == DCS) {
		if (i == 1) {
			dsi.msg.type = DTYPE_DCS_WRITE;;
		} else if (i == 2) {
			dsi.msg.type = DTYPE_DCS_WRITE1;
		} else {
			dsi.msg.type = DTYPE_DCS_LWRITE;
		}
	} else {
		if (i == 1) {
			dsi.msg.type = DTYPE_GEN_WRITE1;
		} else if (i == 2) {
			dsi.msg.type = DTYPE_GEN_WRITE2;
		} else {
			dsi.msg.type = DTYPE_GEN_LWRITE;
		}
	}

	dsi.last_command = true;
	dsi.msg.channel = 0;
	dsi.msg.flags = 0;
	dsi.msg.ctrl = 0;
	dsi.post_wait_ms = 0;
	dsi.msg.tx_len = i;
	dsi.msg.tx_buf = data;

	pr_debug("%s: last = %d, vc = %d, ack = %d, wait = %d, dlen = %zd\n",
		__func__,
		dsi.last_command, dsi.msg.channel, dsi.msg.flags, dsi.post_wait_ms,
		dsi.msg.tx_len);

	dsi_panel_tx_cmd(display->panel, &dsi);

	print_params(dsi.msg.type, data[0], i, (u8 *)dsi.msg.tx_buf);

fail_free_all:
	pr_err("END\n");
	return count;
}

static ssize_t dsi_panel_driver_reg_write_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	return scnprintf(buf, PAGE_SIZE, "%s\n", res_buf);
}

static ssize_t dsi_panel_driver_reg_read_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	struct dsi_display *display = dev_get_drvdata(dev);
	struct dsi_cmd_desc dsi;
	struct dsi_display_ctrl *m_ctrl;
	u8 data[3]; /* No more than reg + two parameters is allowed */
	int i = 0;
	int j = 0;
	int ret;
	char *rbuf;
	int nbr_bytes_to_read;
	int size_rbuf = 0;
	const char *p;

	enum dbg_cmd_type cmd;

	if (!display->panel->spec_pdata->display_onoff_state) {
		pr_err("%s: panel is NOT on\n", __func__);
		goto fail_free_all;
	}

	m_ctrl = &display->ctrl[display->cmd_master_idx];

	reset_res_buf();

	ret = get_cmd_type((char *)buf, &cmd);
	if (ret) {
		update_res_buf("Read - unknown type\n");
		goto fail_free_all;
	}

	p = buf;
	p = p+4;

	/* Get nbr_bytes_to_read */
	if (sscanf(p, "%d", &nbr_bytes_to_read) != 1) {
		update_res_buf("Read - parameter error\n");
		goto fail_free_all;
	}

	while (isxdigit(*p) || (*p == 'x'))
		p++;

	ret = get_parameters(p, data, ARRAY_SIZE(data), &i);
	if (ret)
		goto fail_free_all;

	if (cmd == DCS) {
		dsi.msg.type = DTYPE_DCS_READ;
	} else {
		if (i == 1) {
			dsi.msg.type = DTYPE_GEN_READ1;
		} else {
			dsi.msg.type = DTYPE_GEN_READ2;
		}
	}

	dsi.last_command = true;
	dsi.msg.channel = 0;
	dsi.msg.flags = 0;
	dsi.msg.ctrl = 0;
	dsi.post_wait_ms = 5;
	dsi.msg.tx_len = i;
	dsi.msg.tx_buf = data;
	dsi.msg.wait_ms = 0;

	for (j = 0; j < i; j++)
		pr_debug("%s: tx_buf[%d] = 0x%x\n", __func__, j,
			(*((data)+(sizeof(u8)) * j)));

	if (nbr_bytes_to_read <= 2)
		size_rbuf = 4;
	else
		size_rbuf = nbr_bytes_to_read + 13;

	rbuf = kcalloc(size_rbuf, sizeof(char), GFP_KERNEL);
	if (!rbuf)
		goto fail_free_all;

	dsi_panel_rx_cmd(display, &dsi, m_ctrl, rbuf, nbr_bytes_to_read);

	print_params(dsi.msg.type, data[0], nbr_bytes_to_read, (u8 *)rbuf);

	if (rbuf)
		kzfree(rbuf);
fail_free_all:
	return count;
}

static ssize_t dsi_panel_driver_reg_read_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	return scnprintf(buf, PAGE_SIZE, "%s\n", res_buf);
}

static void dsi_panel_driver_update_areacount(struct dsi_panel *panel, int new_area)
{
	u32 now;
	u32 duration;
	struct panel_specific_pdata *spec_pdata = NULL;

	spec_pdata = panel->spec_pdata;

	now = jiffies;
	duration = (now - spec_pdata->start_jiffies) >= 0 ?
		(now - spec_pdata->start_jiffies) : (now + spec_pdata->start_jiffies);
	spec_pdata->area_count[spec_pdata->now_area]
			= spec_pdata->area_count[spec_pdata->now_area]
				+ jiffies_to_msecs(duration);

	spec_pdata->now_area = new_area;
	spec_pdata->start_jiffies = now;
}

static ssize_t dsi_panel_driver_area_count_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{

	struct dsi_display *display = dev_get_drvdata(dev);
	int i;
	char area_count_str[DSI_BUF_SIZE];
	char count_data[BR_MAX_FIGURE];
	struct panel_specific_pdata *spec_pdata = NULL;

	spec_pdata = display->panel->spec_pdata;

	if (!spec_pdata->area_count_table_size)
		return snprintf(buf, DSI_BUF_SIZE, "area_count is not supported\n");

	mutex_lock(&display->display_lock);
	/* fixed statistics */
	dsi_panel_driver_update_areacount(display->panel, spec_pdata->now_area);

	memset(area_count_str, 0, sizeof(char) * DSI_BUF_SIZE);
	for (i = 0; i < spec_pdata->area_count_table_size; i++) {
		pr_debug("%ld, ", spec_pdata->area_count[i]);
		memset(count_data, 0, sizeof(char) * BR_MAX_FIGURE);
		if (spec_pdata->area_count[i] > AREA_COUNT_MAX) {
			/* over 167 min */
			spec_pdata->area_count[i] = AREA_COUNT_MAX;
		}
		if (i == 0) {
			snprintf(count_data, BR_MAX_FIGURE,
				"%ld", spec_pdata->area_count[i]);
			strlcpy(area_count_str, count_data, DSI_BUF_SIZE);
		} else {
			snprintf(count_data, BR_MAX_FIGURE,
				",%ld", spec_pdata->area_count[i]);
			strlcat(area_count_str, count_data, DSI_BUF_SIZE);
		}
	}

	memset(spec_pdata->area_count, 0, sizeof(u32) * spec_pdata->area_count_table_size);
	mutex_unlock(&display->display_lock);

	return snprintf(buf, DSI_BUF_SIZE, "%s\n", area_count_str);
}

static struct device_attribute panel_attributes[] = {
	__ATTR(panel_id, S_IRUSR, dsi_panel_driver_id_show, NULL),
	__ATTR(panel_reg_write,  S_IRUSR|S_IRGRP|S_IWUSR|S_IWGRP,
		dsi_panel_driver_reg_write_show,
		dsi_panel_driver_reg_write_store),
	__ATTR(panel_reg_read,  S_IRUSR|S_IRGRP|S_IWUSR|S_IWGRP,
		dsi_panel_driver_reg_read_show,
		dsi_panel_driver_reg_read_store),
	__ATTR(pre_sod_mode, (S_IRUSR|S_IRGRP|S_IWUSR|S_IWGRP),
		dsi_panel_pre_sod_mode_show,
		dsi_panel_pre_sod_mode_store),
	__ATTR(area_count, (S_IRUSR | S_IRGRP),
		dsi_panel_driver_area_count_show,
		NULL),
	__ATTR(hbm_mode, S_IRUSR|S_IRGRP|S_IWUSR|S_IWGRP,
		dsi_panel_driver_hbm_mode_show,
		dsi_panel_driver_hbm_mode_store),
	__ATTR(flm2_mode, (S_IWUSR|S_IWGRP),
		NULL,
		dsi_panel_driver_flm2_mode_store),
	__ATTR(opec_mode, S_IRUSR|S_IRGRP|S_IWUSR|S_IWGRP,
		dsi_panel_driver_opec_mode_show,
		dsi_panel_driver_opec_mode_store),
};

static u32 dsi_panel_driver_get_area(struct dsi_panel *panel, u32 level)
{
	int i;
	struct panel_specific_pdata *spec_pdata = NULL;

	spec_pdata = panel->spec_pdata;

	for (i = 0; i < spec_pdata->area_count_table_size; i++) {
		if (i == 0) {
			if (level == spec_pdata->area_count_table[i])
				break;
		} else {
			if (level <= spec_pdata->area_count_table[i])
				break;
		}
	}

	return i;
}

void dsi_panel_driver_panel_update_area(struct dsi_panel *panel, u32 level)
{
	struct panel_specific_pdata *spec_pdata = NULL;
	u32 new_area = dsi_panel_driver_get_area(panel, level);

	spec_pdata = panel->spec_pdata;

	if (spec_pdata->now_area != new_area)
		dsi_panel_driver_update_areacount(panel, new_area);
}

void dsi_panel_driver_init_area_count(struct dsi_panel *panel)
{
	struct panel_specific_pdata *spec_pdata = NULL;

	spec_pdata = panel->spec_pdata;

	spec_pdata->now_area = 0;
	spec_pdata->start_jiffies = jiffies;
}

void dsi_panel_driver_deinit_area_count(struct dsi_panel *panel)
{
	struct panel_specific_pdata *spec_pdata = NULL;

	spec_pdata = panel->spec_pdata;

	if (spec_pdata->area_count_table)
		kfree(spec_pdata->area_count_table);
	if (spec_pdata->area_count)
		kfree(spec_pdata->area_count);
}

static int dsi_panel_driver_panel_register_attributes(struct device *dev)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(panel_attributes); i++)
		if (device_create_file(dev, panel_attributes + i))
			goto error;
	return 0;

error:
	dev_err(dev, "%s: Unable to create interface\n", __func__);

	for (--i; i >= 0 ; i--)
		device_remove_file(dev, panel_attributes + i);
	return -ENODEV;
}

int dsi_panel_driver_create_fs(struct dsi_display *display)
{
	int rc = 0;
	char *path_name = "dsi_panel_driver";

	dev_set_name(&virtdev, "%s", path_name);
	rc = device_register(&virtdev);
	if (rc) {
		pr_err("%s: device_register rc = %d\n", __func__, rc);
		goto err;
	}

	rc = dsi_panel_driver_panel_register_attributes(&virtdev);
	if (rc) {
		device_unregister(&virtdev);
		goto err;
	}
	dev_set_drvdata(&virtdev, display);
err:
	return rc;
}

static irqreturn_t dsi_panel_driver_oled_short_det_handler(int irq, void *dev)
{
	struct dsi_display *display = (struct dsi_display *)dev;
	struct short_detection_ctrl *short_det = NULL;

	if (dev == NULL) {
		pr_err("%s: Invalid parameter\n", __func__);
		return IRQ_HANDLED;
	}
	short_det = &display->panel->spec_pdata->short_det;

	if (short_det == NULL) {
		pr_err("%s: NULL pointer detected\n", __func__);
		return IRQ_HANDLED;
	}

	pr_err("%s: VREG_NG interrupt!\n", __func__);

	if (gpio_get_value(display->panel->spec_pdata->disp_err_fg_gpio) == 1)
		pr_err("%s: VREG NG!!!\n", __func__);
	else
		return IRQ_HANDLED;

	if (short_det->short_check_working) {
		pr_debug("%s already being check work.\n", __func__);
		return IRQ_HANDLED;
	}

	short_det->current_chatter_cnt = SHORT_CHATTER_CNT_START;

	schedule_delayed_work(&short_det->check_work,
		msecs_to_jiffies(short_det->target_chatter_check_interval));

	return IRQ_HANDLED;
}

void dsi_panel_driver_oled_short_det_enable(
		struct panel_specific_pdata *spec_pdata, bool inwork)
{
	struct short_detection_ctrl *short_det = NULL;

	if (spec_pdata == NULL) {
		pr_err("%s: Invalid parameter\n", __func__);
		return;
	}
	short_det = &spec_pdata->short_det;

	if (short_det == NULL) {
		pr_err("%s: NULL pointer detected\n", __func__);
		return;
	}

	if (short_det->short_check_working && !inwork) {
		pr_debug("%s: short_check_worker is already being processed.\n", __func__);
		return;
	}

	if (short_det->irq_enable)
		return;

	short_det->irq_enable = true;
	enable_irq(short_det->irq_num);

	return;
}

void dsi_panel_driver_oled_short_det_disable(
		struct panel_specific_pdata *spec_pdata)
{
	struct short_detection_ctrl *short_det = NULL;

	if (spec_pdata == NULL) {
		pr_err("%s: Invalid parameter\n", __func__);
		return;
	}
	short_det = &spec_pdata->short_det;

	if (short_det == NULL) {
		pr_err("%s: NULL pointer detected\n", __func__);
		return;
	}

	disable_irq(short_det->irq_num);
	short_det->irq_enable = false;

	return;
}

void dsi_panel_driver_oled_short_det_init_works(struct dsi_display *display)
{
	struct short_detection_ctrl *short_det = NULL;
	int rc = 0;

	if (display == NULL) {
		pr_err("%s: Invalid parameter\n", __func__);
		return;
	}
	short_det = &display->panel->spec_pdata->short_det;

	INIT_DELAYED_WORK(&short_det->check_work,
				dsi_panel_driver_oled_short_check_worker);

	short_det->current_chatter_cnt = 0;
	short_det->short_check_working = false;
	short_det->target_chatter_check_interval =
				SHORT_DEFAULT_TARGET_CHATTER_INTERVAL;

	if (!gpio_is_valid(display->panel->spec_pdata->disp_err_fg_gpio)) {
		pr_err("%s: disp error flag gpio is invalid\n", __func__);
		return;
	}
	short_det->irq_num = gpio_to_irq(display->panel->spec_pdata->disp_err_fg_gpio);

	rc = request_irq(short_det->irq_num,
			dsi_panel_driver_oled_short_det_handler,
			SHORT_IRQF_FLAGS, "disp_err_fg_gpio", display);
	if (rc < 0) {
		pr_err("Failed to irq request rc=%d\n", rc);
		return;
	}

	dsi_panel_driver_oled_short_det_disable(display->panel->spec_pdata);
	if (display->boot_disp->boot_disp_en)
		dsi_panel_driver_oled_short_det_enable(
			display->panel->spec_pdata, SHORT_WORKER_PASSIVE);

	//For error flag already rised in xboot case
	if(gpio_get_value(display->panel->spec_pdata->disp_err_fg_gpio)) {
		pr_err("%s: Error Flag Detected\n", __func__);
		short_det->current_chatter_cnt = SHORT_CHATTER_CNT_START;
		schedule_delayed_work(&short_det->check_work,
			msecs_to_jiffies(short_det->target_chatter_check_interval));
	}

}

void dsi_panel_driver_oled_short_check_worker(struct work_struct *work)
{
	int rc = 0;
	struct delayed_work *dwork;
	struct short_detection_ctrl *short_det;
	struct panel_specific_pdata *spec_pdata;

	if (work == NULL) {
		pr_err("%s: Invalid parameter\n", __func__);
		return;
	}
	dwork = to_delayed_work(work);

	short_det = container_of(dwork, struct short_detection_ctrl, check_work);
	spec_pdata = container_of(short_det, struct panel_specific_pdata, short_det);

	if (spec_pdata == NULL || short_det == NULL) {
		pr_err("%s: Null pointer detected\n", __func__);
		return;
	}

	if (!spec_pdata->display_onoff_state) {
		pr_err("%s: power status failed\n", __func__);
		return;
	}

	if (short_det->short_check_working) {
		pr_debug("%s: already status checked\n", __func__);
		return;
	}
	short_det->short_check_working = true;

	if (short_det->current_chatter_cnt == SHORT_CHATTER_CNT_START)
		dsi_panel_driver_oled_short_det_disable(spec_pdata);

	/* status check */
	rc = gpio_get_value(spec_pdata->disp_err_fg_gpio);
	if (rc > 0) {
		short_det->current_chatter_cnt++;
		pr_err("%s: Short Detection [%d]\n",
				__func__, short_det->current_chatter_cnt);
		if (short_det->current_chatter_cnt >=
				SHORT_DEFAULT_TARGET_CHATTER_CNT) {
			pr_err("%s: execute shutdown.\n", __func__);

			/* shutdown */
			for (;;) {
				pm_power_off();
				msleep(SHORT_POWER_OFF_RETRY_INTERVAL);
			}
			return;
		}

		short_det->short_check_working = false;
		schedule_delayed_work(&short_det->check_work,
			msecs_to_jiffies(short_det->target_chatter_check_interval));
		return;
	}
	dsi_panel_driver_oled_short_det_enable(spec_pdata, SHORT_WORKER_ACTIVE);

	/* reset count*/
	short_det->current_chatter_cnt = 0;
	short_det->short_check_working = false;

	pr_debug("%s: short_check_worker done.\n", __func__);
	return;
}
