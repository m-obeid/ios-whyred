/*
 * vibe [ID]: AudioServicesPlaySystemSound(ID), default kSystemSoundID_Vibrate (4095), then wait 3 s.
 * whyred testing: with whyred_ui injected ("vibe" in kern.exec_inject) a vibrating ID drives the motor;
 * any sound ID plays through mediaserverd and the HAL plug-in (RedwoodAudio.driver).
 */
extern void AudioServicesPlaySystemSound(unsigned int id);
extern unsigned int sleep(unsigned int s);
extern int atoi(const char *s);
int
main(int argc, char **argv)
{
	AudioServicesPlaySystemSound(argc > 1 ? (unsigned int)atoi(argv[1]) : 4095u);
	sleep(3);
	return 0;
}
