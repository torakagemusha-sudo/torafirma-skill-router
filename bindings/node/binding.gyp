{
  "targets": [
    {
      "target_name": "skillrouter",
      "sources": [
        "addon.cpp",
        "../../skill-router/skilllib_c.cpp",
        "../../skill-router/third_party/sqlite3.c"
      ],
      "include_dirs": [
        "../../skill-router"
      ],
      "defines": [
        "SQLITE_THREADSAFE=1",
        "SQLITE_ENABLE_FTS5"
      ],
      "cflags_cc!": ["-fno-exceptions"],
      "cflags_cc": ["-std=c++20", "-fexceptions"],
      "conditions": [
        ["OS=='win'", {
          "msvs_settings": {
            "VCCLCompilerTool": {
              "AdditionalOptions": ["/std:c++20", "/EHsc"]
            }
          }
        }],
        ["OS=='linux'", {
          "libraries": ["-lpthread", "-ldl"]
        }]
      ]
    }
  ]
}
