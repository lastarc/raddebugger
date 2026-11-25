// Copyright (c) Epic Games Tools
// Licensed under the MIT license (https://opensource.org/license/mit/)

#ifndef MACHO_H
#define MACHO_H

////////////////////////////////
//~ Mach-O Constants

#define MACHO_MAGIC_32           0xfeedfaceu
#define MACHO_MAGIC_64           0xfeedfacfu
#define MACHO_CIGAM_32           0xcefaedfeu
#define MACHO_CIGAM_64           0xcffaedfeu
#define MACHO_FAT_MAGIC          0xcafebabeu // stored big-endian
#define MACHO_FAT_MAGIC_64       0xcafebabfu // stored big-endian

#define MACHO_CPU_ARCH_ABI64     0x01000000u
#define MACHO_CPU_TYPE_X86       7u
#define MACHO_CPU_TYPE_X86_64    (MACHO_CPU_TYPE_X86|MACHO_CPU_ARCH_ABI64)
#define MACHO_CPU_TYPE_ARM       12u
#define MACHO_CPU_TYPE_ARM64     (MACHO_CPU_TYPE_ARM|MACHO_CPU_ARCH_ABI64)

typedef U32 MACHO_FileType;
enum
{
  MACHO_FileType_Object     = 0x1,
  MACHO_FileType_Execute    = 0x2,
  MACHO_FileType_Dylib      = 0x6,
  MACHO_FileType_Dylinker   = 0x7,
  MACHO_FileType_Bundle     = 0x8,
  MACHO_FileType_DSYM       = 0xa,
};

#define MACHO_LC_REQ_DYLD        0x80000000u
typedef U32 MACHO_LoadCommandKind;
enum
{
  MACHO_LoadCommandKind_Symtab     = 0x2,
  MACHO_LoadCommandKind_IdDylib    = 0xd,
  MACHO_LoadCommandKind_Segment64  = 0x19,
  MACHO_LoadCommandKind_UUID       = 0x1b,
  MACHO_LoadCommandKind_Main       = (0x28|MACHO_LC_REQ_DYLD),
};

// nlist n_type bits
#define MACHO_N_STAB  0xe0u
#define MACHO_N_TYPE  0x0eu
#define MACHO_N_EXT   0x01u
#define MACHO_N_SECT  0x0eu
#define MACHO_N_OSO   0x66u // debug map: object file name

// vm protections
#define MACHO_VM_PROT_READ    0x1u
#define MACHO_VM_PROT_WRITE   0x2u
#define MACHO_VM_PROT_EXECUTE 0x4u

////////////////////////////////
//~ Mach-O File Structures

typedef struct MACHO_Header64 MACHO_Header64;
struct MACHO_Header64
{
  U32 magic;
  U32 cputype;
  U32 cpusubtype;
  U32 filetype;
  U32 ncmds;
  U32 sizeofcmds;
  U32 flags;
  U32 reserved;
};

typedef struct MACHO_LoadCommand MACHO_LoadCommand;
struct MACHO_LoadCommand
{
  U32 cmd;
  U32 cmdsize;
};

typedef struct MACHO_SegmentCommand64 MACHO_SegmentCommand64;
struct MACHO_SegmentCommand64
{
  U32 cmd;
  U32 cmdsize;
  U8  segname[16];
  U64 vmaddr;
  U64 vmsize;
  U64 fileoff;
  U64 filesize;
  U32 maxprot;
  U32 initprot;
  U32 nsects;
  U32 flags;
};

typedef struct MACHO_Section64 MACHO_Section64;
struct MACHO_Section64
{
  U8  sectname[16];
  U8  segname[16];
  U64 addr;
  U64 size;
  U32 offset;
  U32 align;
  U32 reloff;
  U32 nreloc;
  U32 flags;
  U32 reserved1;
  U32 reserved2;
  U32 reserved3;
};

typedef struct MACHO_SymtabCommand MACHO_SymtabCommand;
struct MACHO_SymtabCommand
{
  U32 cmd;
  U32 cmdsize;
  U32 symoff;
  U32 nsyms;
  U32 stroff;
  U32 strsize;
};

typedef struct MACHO_UUIDCommand MACHO_UUIDCommand;
struct MACHO_UUIDCommand
{
  U32 cmd;
  U32 cmdsize;
  U8  uuid[16];
};

typedef struct MACHO_EntryPointCommand MACHO_EntryPointCommand;
struct MACHO_EntryPointCommand
{
  U32 cmd;
  U32 cmdsize;
  U64 entryoff;
  U64 stacksize;
};

typedef struct MACHO_Nlist64 MACHO_Nlist64;
struct MACHO_Nlist64
{
  U32 n_strx;
  U8  n_type;
  U8  n_sect;
  U16 n_desc;
  U64 n_value;
};

// NOTE: fat headers are stored big-endian
typedef struct MACHO_FatHeader MACHO_FatHeader;
struct MACHO_FatHeader
{
  U32 magic;
  U32 nfat_arch;
};

typedef struct MACHO_FatArch MACHO_FatArch;
struct MACHO_FatArch
{
  U32 cputype;
  U32 cpusubtype;
  U32 offset;
  U32 size;
  U32 align;
};

typedef struct MACHO_FatArch64 MACHO_FatArch64;
struct MACHO_FatArch64
{
  U32 cputype;
  U32 cpusubtype;
  U64 offset;
  U64 size;
  U32 align;
  U32 reserved;
};

////////////////////////////////
//~ Compact Unwind (__TEXT,__unwind_info) Structures

#define MACHO_UNWIND_SECTION_VERSION               1u
#define MACHO_UNWIND_SECOND_LEVEL_REGULAR          2u
#define MACHO_UNWIND_SECOND_LEVEL_COMPRESSED       3u

#define MACHO_UNWIND_X86_64_MODE_MASK              0x0F000000u
#define MACHO_UNWIND_X86_64_MODE_RBP_FRAME         0x01000000u
#define MACHO_UNWIND_X86_64_MODE_STACK_IMMD        0x02000000u
#define MACHO_UNWIND_X86_64_MODE_STACK_IND         0x03000000u
#define MACHO_UNWIND_X86_64_MODE_DWARF             0x04000000u
#define MACHO_UNWIND_X86_64_RBP_FRAME_REGISTERS    0x00007FFFu
#define MACHO_UNWIND_X86_64_RBP_FRAME_OFFSET       0x00FF0000u
#define MACHO_UNWIND_X86_64_FRAMELESS_STACK_SIZE   0x00FF0000u
#define MACHO_UNWIND_X86_64_FRAMELESS_STACK_ADJUST 0x0000E000u
#define MACHO_UNWIND_X86_64_FRAMELESS_STACK_REG_COUNT 0x00001C00u
#define MACHO_UNWIND_X86_64_FRAMELESS_STACK_REG_PERMUTATION 0x000003FFu
#define MACHO_UNWIND_X86_64_DWARF_SECTION_OFFSET   0x00FFFFFFu

typedef enum MACHO_UnwindX64Reg
{
  MACHO_UnwindX64Reg_None = 0,
  MACHO_UnwindX64Reg_RBX  = 1,
  MACHO_UnwindX64Reg_R12  = 2,
  MACHO_UnwindX64Reg_R13  = 3,
  MACHO_UnwindX64Reg_R14  = 4,
  MACHO_UnwindX64Reg_R15  = 5,
  MACHO_UnwindX64Reg_RBP  = 6,
}
MACHO_UnwindX64Reg;

typedef struct MACHO_UnwindInfoHeader MACHO_UnwindInfoHeader;
struct MACHO_UnwindInfoHeader
{
  U32 version;
  U32 common_encodings_array_section_offset;
  U32 common_encodings_array_count;
  U32 personality_array_section_offset;
  U32 personality_array_count;
  U32 index_section_offset;
  U32 index_count;
};

typedef struct MACHO_UnwindInfoIndexEntry MACHO_UnwindInfoIndexEntry;
struct MACHO_UnwindInfoIndexEntry
{
  U32 function_offset;
  U32 second_level_pages_section_offset;
  U32 lsda_index_array_section_offset;
};

typedef struct MACHO_UnwindInfoRegularPageHeader MACHO_UnwindInfoRegularPageHeader;
struct MACHO_UnwindInfoRegularPageHeader
{
  U32 kind;
  U16 entry_page_offset;
  U16 entry_count;
};

typedef struct MACHO_UnwindInfoRegularEntry MACHO_UnwindInfoRegularEntry;
struct MACHO_UnwindInfoRegularEntry
{
  U32 function_offset;
  U32 encoding;
};

typedef struct MACHO_UnwindInfoCompressedPageHeader MACHO_UnwindInfoCompressedPageHeader;
struct MACHO_UnwindInfoCompressedPageHeader
{
  U32 kind;
  U16 entry_page_offset;
  U16 entry_count;
  U16 encodings_page_offset;
  U16 encodings_count;
};

////////////////////////////////
//~ Helpers

internal Arch arch_from_macho_cputype(U32 cputype);

#endif // MACHO_H
