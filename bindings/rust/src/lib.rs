use serde_json::Value;
use std::ffi::{CStr, CString};
use std::ptr;

#[allow(non_camel_case_types, non_snake_case, non_upper_case_globals)]
mod ffi {
    include!(concat!(env!("OUT_DIR"), "/bindings.rs"));
}

#[derive(Debug)]
pub struct Error {
    pub status: i32,
    pub message: String,
}

pub type Result<T> = std::result::Result<T, Error>;

pub struct SkillLibrary {
    raw: *mut ffi::skilllib_t,
}

impl SkillLibrary {
    pub fn open(catalog: &str, telemetry: &str, read_only: bool) -> Result<Self> {
        let catalog = cstring(catalog)?;
        let telemetry = cstring(telemetry)?;
        let mut raw = ptr::null_mut();
        let status = unsafe {
            ffi::skilllib_open(
                catalog.as_ptr(),
                telemetry.as_ptr(),
                if read_only { 1 } else { 0 },
                &mut raw,
            )
        };
        if status != ffi::skilllib_status_t::SKILLLIB_OK {
            return Err(Error {
                status: status as i32,
                message: "skilllib_open failed".into(),
            });
        }
        Ok(Self { raw })
    }

    pub fn register(&mut self, path: &str, keywords: &str) -> Result<Value> {
        let path = cstring(path)?;
        let keywords = cstring(keywords)?;
        let raw = self.raw;
        self.json_call(|out| unsafe {
            ffi::skilllib_register(raw, path.as_ptr(), keywords.as_ptr(), out)
        })
    }

    pub fn search(
        &mut self,
        query: &str,
        top_n: i32,
        mode: &str,
        include_archived: bool,
    ) -> Result<Value> {
        let query = cstring(query)?;
        let mode = cstring(mode)?;
        let raw = self.raw;
        self.json_call(|out| unsafe {
            ffi::skilllib_search(
                raw,
                query.as_ptr(),
                top_n,
                mode.as_ptr(),
                if include_archived { 1 } else { 0 },
                out,
            )
        })
    }

    pub fn fetch(
        &mut self,
        skill_id: &str,
        expected_revision: &str,
        catalog_generation: &str,
        query_context: &str,
    ) -> Result<Value> {
        let skill_id = cstring(skill_id)?;
        let expected_revision = cstring(expected_revision)?;
        let catalog_generation = cstring(catalog_generation)?;
        let query_context = cstring(query_context)?;
        let raw = self.raw;
        self.json_call(|out| unsafe {
            ffi::skilllib_fetch(
                raw,
                skill_id.as_ptr(),
                query_context.as_ptr(),
                expected_revision.as_ptr(),
                catalog_generation.as_ptr(),
                out,
            )
        })
    }

    pub fn catalog_generation(&mut self) -> Result<String> {
        let mut out = ffi::skilllib_buffer_t {
            data: ptr::null_mut(),
            len: 0,
        };
        let status = unsafe { ffi::skilllib_catalog_generation(self.raw, &mut out) };
        if status != ffi::skilllib_status_t::SKILLLIB_OK {
            return Err(self.error(status));
        }
        let bytes =
            unsafe { std::slice::from_raw_parts(out.data as *const u8, out.len) }.to_vec();
        unsafe { ffi::skilllib_buffer_free(&mut out) };
        String::from_utf8(bytes).map_err(|e| Error {
            status: -1,
            message: e.to_string(),
        })
    }

    pub fn version() -> &'static str {
        unsafe { CStr::from_ptr(ffi::skilllib_version()) }
            .to_str()
            .expect("engine version is valid UTF-8")
    }

    pub fn ranking_policy() -> &'static str {
        unsafe { CStr::from_ptr(ffi::skilllib_ranking_policy()) }
            .to_str()
            .expect("ranking policy is valid UTF-8")
    }

    fn json_call<F>(&mut self, call: F) -> Result<Value>
    where
        F: FnOnce(*mut ffi::skilllib_buffer_t) -> ffi::skilllib_status_t,
    {
        let mut out = ffi::skilllib_buffer_t {
            data: ptr::null_mut(),
            len: 0,
        };
        let status = call(&mut out);
        if status != ffi::skilllib_status_t::SKILLLIB_OK {
            return Err(self.error(status));
        }
        let bytes =
            unsafe { std::slice::from_raw_parts(out.data as *const u8, out.len) }.to_vec();
        unsafe { ffi::skilllib_buffer_free(&mut out) };
        serde_json::from_slice(&bytes).map_err(|e| Error {
            status: -1,
            message: e.to_string(),
        })
    }

    fn error(&self, status: ffi::skilllib_status_t) -> Error {
        let message = unsafe {
            let ptr = ffi::skilllib_last_error(self.raw);
            if ptr.is_null() {
                String::new()
            } else {
                CStr::from_ptr(ptr).to_string_lossy().into_owned()
            }
        };
        Error {
            status: status as i32,
            message,
        }
    }
}

impl Drop for SkillLibrary {
    fn drop(&mut self) {
        if !self.raw.is_null() {
            unsafe { ffi::skilllib_close(self.raw) };
            self.raw = ptr::null_mut();
        }
    }
}

fn cstring(value: &str) -> Result<CString> {
    CString::new(value).map_err(|e| Error {
        status: -1,
        message: e.to_string(),
    })
}
