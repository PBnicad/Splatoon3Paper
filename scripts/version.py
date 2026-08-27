# PlatformIO pre-script: inject custom_fw_version as the FW_VERSION compiler
# macro so local and CI builds share one source of truth. Writes no files.

Import("env")

ver = env.GetProjectOption("custom_fw_version", "0.0.0")
# escape the quotes for the command line so the C macro expands to a literal
env.Append(CPPDEFINES=[("FW_VERSION", '\\"%s\\"' % ver)])
print("[version] FW_VERSION = %s" % ver)
