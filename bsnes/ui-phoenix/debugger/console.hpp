struct Console : TopLevelWindow {
  EditBox output;
  CheckBox traceToConsole;
  CheckBox traceToDisk;
  CheckBox traceCPU;
  CheckBox traceSMP;

  string buffer;

  void create();
  void eventTraceCPU();
  void eventTraceSMP();
};

extern Console console;
