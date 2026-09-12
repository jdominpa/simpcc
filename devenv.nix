{
  pkgs,
  ...
}:
{
  languages.c.enable = true;
  packages = [ pkgs.lldb ];
  env.LLDB_DEBUGSERVER_PATH = "/Library/Developer/CommandLineTools/Library/PrivateFrameworks/LLDB.framework/Resources/debugserver";
}
