/*
===========================================================================

MinGW stub for Microsoft CodeAnalysis SourceAnnotations.h

This is a minimal stub to allow compilation with MinGW-w64.
The actual Microsoft header provides SAL (Source Code Annotation Language)
annotations for static analysis. We provide empty macros for MinGW.

===========================================================================
*/

#ifndef _CODEANALYSIS_SOURCEANNOTATIONS_H_
#define _CODEANALYSIS_SOURCEANNOTATIONS_H_

#if defined(__GNUC__) && !defined(_MSC_VER)
// MinGW / GCC - provide empty macros
#define _Pre_equal_to_(x)
#define _Post_equal_to_(x)
#define _Pre_notnull_
#define _Post_notnull_
#define _Pre_null_
#define _Post_null_
#define _Pre_satisfies_(expr)
#define _Post_satisfies_(expr)
#define _Deref_pre_equal_to_(x)
#define _Deref_post_equal_to_(x)
#define _Deref_pre_notnull_
#define _Deref_post_notnull_
#define _Deref_pre_null_
#define _Deref_post_null_
#define _Deref_pre_satisfies_(expr)
#define _Deref_post_satisfies_(expr)
#define _In_
#define _Out_
#define _Inout_
#define _Outptr_
#define _Outptr_result_maybenull_
#define _In_opt_
#define _Out_opt_
#define _Inout_opt_
#define _Outptr_opt_
#define _In_reads_(x)
#define _Out_writes_(x)
#define _Inout_updates_(x)
#define _Out_writes_bytes_(x)
#define _In_reads_bytes_(x)
#define _Out_writes_bytes_all_(x)
#define _In_reads_z_
#define _Out_writes_z_
#define _In_reads_or_z_
#define _Out_writes_z_
#define _Field_size_(x)
#define _Field_size_bytes_(x)
#define _Field_size_opt_(x)
#define _Field_size_bytes_opt_(x)
#define _COM_Outptr_
#define _COM_Outptr_opt_
#define _Result_zeroonfailure
#define _Success_(expr)
#define _On_failure_(expr)
#define _Ret_maybenull_
#define _Ret_notnull_
#define _Ret_null_
#define _Ret_z_
#define _Pre_unknown_
#define _Post_unknown_
#define _Ret_unknown_
#define _Analysis_assume_(expr)
#define _Assume_(expr)

#endif // __GNUC__

#endif // _CODEANALYSIS_SOURCEANNOTATIONS_H_