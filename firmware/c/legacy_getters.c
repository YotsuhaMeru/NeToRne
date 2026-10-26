/* First executable C integration: replace only two verified legacy getters.
 * This retains the existing PSI experiment's behavior; it is NOT raw TS.
 * The original callers consume the unsigned return value in FR30 r4.
 */
unsigned int __attribute__((section(".trc_es"))) trc_es_selection(void)
{
    return 4;
}

unsigned int __attribute__((section(".trc_pmt"))) trc_pmt_output(void)
{
    return 4;
}
