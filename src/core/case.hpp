#pragma once

namespace mag {

namespace Case_Handling_ {
enum Case_Handling {
    /// Letters will only match those that match their case.
    CASE_SENSITIVE,

    /// Letters will match a letter of the any case.
    CASE_INSENSITIVE,

    /// Uppercase letters are treated as case sensitive whereas
    /// lowercase letters are treated as case insensitive.
    UPPERCASE_STICKY,

    /// If there are any uppercase letters then `CASE_SENSITIVE`
    /// will be used, otherwise `CASE_INSENSITIVE` will be used.
    SMART_CASE,
};
}
using Case_Handling_::Case_Handling;

}
