    /** Data type used for this configuration key. */
    const DataType *data_type() const;
    /** String identifier for this configuration key, suitable for CLI use. */
    string identifier() const;
    /** Description of this configuration key. */
    string description() const;
    /** Get configuration key by string identifier. */
    static const ConfigKey *get_by_identifier(string identifier);
    /** Parse a string argument into the appropriate type for this key. */
    Glib::VariantBase parse_string(string value) const;
