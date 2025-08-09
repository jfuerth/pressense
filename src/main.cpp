extern "C" {
  int app_main(void);
  int main(void);
}

int app_main(void) {
  return 0;
}

int main(void) {
  return app_main();
}