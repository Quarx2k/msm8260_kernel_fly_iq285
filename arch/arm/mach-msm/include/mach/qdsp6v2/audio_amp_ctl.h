/* Copyright (c) 2010, Code Aurora Forum. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above
 *       copyright notice, this list of conditions and the following
 *       disclaimer in the documentation and/or other materials provided
 *       with the distribution.
 *     * Neither the name of Code Aurora Forum, Inc. nor the names of its
 *       contributors may be used to endorse or promote products derived
 *       from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED "AS IS" AND ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 * BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
 * OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN
 * IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */
#ifndef __MACH_QDSP6V2_AUDIO_AMP_H__
#define __MACH_QDSP6V2_AUDIO_AMP_H__

#include "audio_amp_def.h"

struct audio_amp_ops {
	void (*init)(void);
	void (*exit)(void);	
	void (*set)(int type, int value);
	void (*get)(int type, int * value);
	void (*enable)(AMP_PATH_TYPE_E path);	
	void (*disable)(AMP_PATH_TYPE_E path);
};

void audio_amp_init(AMP_DEVICE_E dev);
void audio_amp_exit(void);
void audio_amp_set(int type, int value);
void audio_amp_get(int type, int * value);
void audio_amp_on(AMP_PATH_TYPE_E path);
void audio_amp_off(AMP_PATH_TYPE_E path);

#ifndef KTTECH_FINAL_BUILD // for final build
#define __APM_FILE__ strrchr(__FILE__, '/') ? (strrchr(__FILE__, '/')+1) : \
	__FILE__

#define APM_DBG(fmt, args...) pr_debug("[%s] " fmt,\
		__func__, ##args)

#define APM_INFO(fmt, args...) pr_info("[%s:%s] " fmt,\
	       __APM_FILE__, __func__, ##args)

#define APM_ERR(fmt, args...) pr_err("[%s:%s] " fmt,\
	       __APM_FILE__, __func__, ##args)
#else
#define APM_DBG(x...) do{}while(0)
#define APM_INFO(x...) do{}while(0)
#define APM_ERR(x...) do{}while(0)
#endif

#endif
