    const DataType *get_data_type() const;
    string get_identifier() const;
    string get_description() const;
    static const ConfigKey *get(string identifier);
    Glib::VariantBase parse_string(string value) const;
