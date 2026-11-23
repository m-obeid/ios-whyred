/* vibe: AudioServicesPlaySystemSound(kSystemSoundID_Vibrate), the system vibration, for testing (whyred). */
extern void AudioServicesPlaySystemSound(unsigned int id);
extern unsigned int sleep(unsigned int s);
int
main(void)
{
	AudioServicesPlaySystemSound(4095);     /* kSystemSoundID_Vibrate */
	sleep(2);
	return 0;
}
