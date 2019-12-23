    /** Data type used for this configuration key. */
    const DataType *data_type() const;
    /** String identifier for this configuration key, suitable for CLI use. */
    std::string identifier() const;
    /** Description of this configuration key. */
    std::string description() const;
    /** Get configuration key by string identifier. */
    static const ConfigKey *get_by_identifier(std::string identifier);
    /** Parse a string argument into the appropriate type for this key. */
    static Glib::VariantBase parse_string(std::string value, enum sr_datatype dt);
    Glib::VariantBase parse_string(std::string value) const;
