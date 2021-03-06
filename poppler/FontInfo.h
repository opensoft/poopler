//========================================================================
//
// FontInfo.h
//
// Copyright (C) 2005 Kristian Høgsberg <krh@redhat.com>
// Copyright (C) 2005-2008, 2010, 2011, 2018 Albert Astals Cid <aacid@kde.org>
// Copyright (C) 2005 Brad Hards <bradh@frogmouth.net>
// Copyright (C) 2009 Pino Toscano <pino@kde.org>
// Copyright (C) 2012 Adrian Johnson <ajohnson@redneon.com>
// Copyright (C) 2013 Thomas Freitag <Thomas.Freitag@alfa.de>
// Copyright (C) 2019 Oliver Sander <oliver.sander@tu-dresden.de>
// Copyright (C) 2019 Adam Reichold <adam.reichold@t-online.de>
//
// To see a description of the changes please see the Changelog file that
// came with your tarball or type make ChangeLog if you are building from git
//
//========================================================================

//========================================================================
//
// Based on code from pdffonts.cc
//
// Copyright 2001-2007 Glyph & Cog, LLC
//
//========================================================================

#ifndef FONT_INFO_H
#define FONT_INFO_H

#include "Object.h"

#include <unordered_set>

class GfxFont;
class PDFDoc;

class FontInfo {
public:
  enum Type {
    unknown,
    Type1,
    Type1C,
    Type1COT,
    Type3,
    TrueType,
    TrueTypeOT,
    CIDType0,
    CIDType0C,
    CIDType0COT,
    CIDTrueType,
    CIDTrueTypeOT
  };
    
  // Constructor.
  FontInfo(GfxFont *fontA, XRef *xrefA);
  // Copy constructor
  FontInfo(FontInfo& f);
  // Destructor.
  ~FontInfo();

  FontInfo& operator=(const FontInfo &) = delete;

  GooString *getName()      { return name; };
  GooString *getSubstituteName() { return substituteName; };
  GooString *getFile()      { return file; };
  GooString *getEncoding()      { return encoding; };
  Type       getType()      { return type; };
  bool      getEmbedded()  { return emb; };
  bool      getSubset()    { return subset; };
  bool      getToUnicode() { return hasToUnicode; };
  Ref        getRef()       { return fontRef; };
  Ref        getEmbRef()    { return embRef; };

private:
  GooString *name;
  GooString *substituteName;
  GooString *file;
  GooString *encoding;
  Type type;
  bool emb;
  bool subset;
  bool hasToUnicode;
  Ref fontRef;
  Ref embRef;
};

class FontInfoScanner {
public:

  // Constructor.
  FontInfoScanner(PDFDoc *doc, int firstPage = 0);
  // Destructor.
  ~FontInfoScanner();

  std::vector<FontInfo*> *scan(int nPages);

private:

  PDFDoc *doc;
  int currentPage;
  std::unordered_set<int> fonts;
  std::unordered_set<int> visitedObjects;

  void scanFonts(XRef *xrefA, Dict *resDict, std::vector<FontInfo*> *fontsList);
};

#endif
