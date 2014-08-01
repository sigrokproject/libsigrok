    /** Data type used for this configuration key. */
    const DataType *get_data_type() const;
    /** String identifier for this configuration key, suitable for CLI use. */
    string get_identifier() const;
    /** Description of this configuration key. */
    string get_description() const;
    /** Get configuration key by string identifier. */
    static const ConfigKey *get(string identifier);
    /** Parse a string argument into the appropriate type for this key. */
    Glib::VariantBase parse_string(string value) const;
