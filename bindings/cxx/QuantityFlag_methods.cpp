vector<const QuantityFlag *>
    QuantityFlag::flags_from_mask(unsigned int mask)
{
    auto result = vector<const QuantityFlag *>();
    while (mask)
    {
        unsigned int new_mask = mask & (mask - 1);
        result.push_back(QuantityFlag::get(
            static_cast<enum sr_mqflag>(mask ^ new_mask)));
        mask = new_mask;
    }
    return result;
}
