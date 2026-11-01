import sys

flags = sys.flags
names = ("debug", "inspect", "interactive", "optimize", "dont_write_bytecode",
         "no_user_site", "no_site", "ignore_environment", "verbose", "bytes_warning",
         "quiet", "hash_randomization", "isolated", "dev_mode", "utf8_mode",
         "warn_default_encoding", "safe_path", "int_max_str_digits")
expected_repr = "sys.flags(" + ", ".join(name + "=" + repr(getattr(flags, name))
                                         for name in names) + ")"
print("repr", repr(flags) == expected_repr)
print("metadata", flags.n_fields, flags.n_sequence_fields, flags.n_unnamed_fields, len(flags))
print("sequence", flags[0], flags[10:13], flags[-1] == flags.int_max_str_digits,
      flags == tuple(flags))
print("bools", type(flags.dev_mode).__name__, type(flags.safe_path).__name__)
try:
    flags.debug = 1
except AttributeError:
    print("readonly")
try:
    type(flags)([])
except TypeError as error:
    print("constructor", str(error))
