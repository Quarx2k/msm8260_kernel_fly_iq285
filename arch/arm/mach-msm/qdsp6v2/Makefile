obj-y += audio_dev_ctl.o
obj-y += board-msm8x60-audio.o
ifdef CONFIG_TIMPANI_CODEC
obj-$(CONFIG_TIMPANI_CODEC) += snddev_icodec.o
else
obj-y += snddev_icodec.o
endif
obj-y += snddev_ecodec.o snddev_mi2s.o
obj-y += apr.o apr_tal.o apr_tal_debug.o dsp_debug.o
obj-y += audio_acdb.o
obj-y += q6asm.o q6adm.o q6afe.o
obj-y += pcm_out.o pcm_in.o fm.o
obj-y += audio_lpa.o
obj-y += q6voice.o
obj-y += snddev_hdmi.o
obj-y += aac_in.o qcelp_in.o evrc_in.o amrnb_in.o audio_utils.o
obj-y += audio_mvs.o
obj-y += audio_wma.o audio_wmapro.o
obj-$(CONFIG_KTTECH_SOUND_AMP) += audio_amp_ctl.o
obj-$(CONFIG_KTTECH_SOUND_YDA165) += snddev_yda165.o
obj-$(CONFIG_KTTECH_SOUND_WM8993) += snddev_wm8993.o
obj-$(CONFIG_KTTECH_SOUND_TUNE) += snddev_tuning.o
obj-$(CONFIG_KTTECH_SND_CODEC_WM8993) += wm8993_kttech.o
