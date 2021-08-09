Sample *process_data(Samples samples)
{
	constexpr float alpha = 0.7;

	static Sample prev = 2048;

	samples[0] = (1 - alpha) * samples[0] + alpha * prev;
	for (unsigned int i = 1; i < samples.size(); i++)
		samples[i] = (1 - alpha) * samples[i] + alpha * samples[i - 1];
	prev = samples[samples.size() - 1];

	return samples.data();
}
