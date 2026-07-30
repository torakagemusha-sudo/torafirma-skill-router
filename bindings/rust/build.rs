use std::env;
use std::path::PathBuf;

fn main() {
    let root = PathBuf::from("../..");
    let skill_router = root.join("skill-router");

    // Emit the dependent C++ archive before SQLite so static link order is
    // skillrouter_cxx -> skillrouter_sqlite on Unix linkers.
    cc::Build::new()
        .cpp(true)
        .file(skill_router.join("skilllib_c.cpp"))
        .include(&skill_router)
        .flag_if_supported("-std=c++20")
        .flag_if_supported("/std:c++20")
        .compile("skillrouter_cxx");

    cc::Build::new()
        .file(skill_router.join("third_party/sqlite3.c"))
        .define("SQLITE_THREADSAFE", "1")
        .define("SQLITE_ENABLE_FTS5", None)
        .compile("skillrouter_sqlite");

    if cfg!(target_family = "unix") {
        println!("cargo:rustc-link-lib=pthread");
        println!("cargo:rustc-link-lib=dl");
    }

    let header = skill_router.join("skilllib_c.h");
    let bindings = bindgen::Builder::default()
        .header(header.to_string_lossy())
        .allowlist_function("skilllib_.*")
        .allowlist_type("skilllib_.*")
        .rustified_enum("skilllib_status_t")
        .generate()
        .expect("unable to generate Skill Router bindings");

    let out = PathBuf::from(env::var("OUT_DIR").unwrap());
    bindings
        .write_to_file(out.join("bindings.rs"))
        .expect("unable to write Skill Router bindings");

    println!("cargo:rerun-if-changed={}", header.display());
    println!(
        "cargo:rerun-if-changed={}",
        skill_router.join("skilllib_c.cpp").display()
    );
    println!(
        "cargo:rerun-if-changed={}",
        skill_router.join("skill_library.hpp").display()
    );
}
