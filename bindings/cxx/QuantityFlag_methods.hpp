	/** Get flags corresponding to a bitmask. */
	static vector<const QuantityFlag *>
		flags_from_mask(unsigned int mask);

	/** Get bitmask corresponding to a set of flags. */
	static unsigned int mask_from_flags(
		vector<const QuantityFlag *> flags);
