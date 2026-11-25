// Copyright (c) Epic Games Tools
// Licensed under the MIT license (https://opensource.org/license/mit/)

internal Arch
arch_from_macho_cputype(U32 cputype)
{
  Arch result = Arch_Null;
  switch(cputype)
  {
    default:{}break;
    case MACHO_CPU_TYPE_X86_64:{result = Arch_x64;}break;
    case MACHO_CPU_TYPE_X86:   {result = Arch_x86;}break;
    case MACHO_CPU_TYPE_ARM64: {result = Arch_arm64;}break;
    case MACHO_CPU_TYPE_ARM:   {result = Arch_arm32;}break;
  }
  return result;
}
