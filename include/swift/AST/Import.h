//===-- Import.h - Representation of imports --------------------*- C++ -*-===//
//
// This source file is part of the Swift.org open source project
//
// Copyright (c) 2014 - 2020 Apple Inc. and the Swift project authors
// Licensed under Apache License v2.0 with Runtime Library Exception
//
// See https://swift.org/LICENSE.txt for license information
// See https://swift.org/CONTRIBUTORS.txt for the list of Swift project authors
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file contains types used to represent information about imports
/// throughout the AST.
///
//===----------------------------------------------------------------------===//

#ifndef SWIFT_IMPORT_H
#define SWIFT_IMPORT_H

#include "swift/AST/Identifier.h"
#include "swift/Basic/Located.h"
#include "swift/Basic/OptionSet.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/DenseMapInfo.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/StringRef.h"
#include <algorithm>

namespace swift {
class ASTContext;
class ModuleDecl;

// MARK: - Fundamental import enums

/// Describes what kind of name is being imported.
///
/// If the enumerators here are changed, make sure to update all diagnostics
/// using ImportKind as a select index.
enum class ImportKind : uint8_t {
  Module = 0,
  Type,
  Struct,
  Class,
  Enum,
  Protocol,
  Var,
  Func
};

/// Possible attributes for imports in source files.
enum class ImportFlags {
  /// The imported module is exposed to anyone who imports the parent module.
  Exported = 0x1,

  /// This source file has access to testable declarations in the imported
  /// module.
  Testable = 0x2,

  /// This source file has access to private declarations in the imported
  /// module.
  PrivateImport = 0x4,

  /// The imported module is an implementation detail of this file and should
  /// not be required to be present if the main module is ever imported
  /// elsewhere.
  ///
  /// Mutually exclusive with Exported.
  ImplementationOnly = 0x8,

  /// The module is imported to have access to named SPIs which is an
  /// implementation detail of this file.
  SPIAccessControl = 0x10,

  /// Used for DenseMap.
  Reserved = 0x80
};

/// \see ImportFlags
using ImportOptions = OptionSet<ImportFlags>;

// MARK: - Import Paths

namespace detail {
  using ImportPathElement = Located<Identifier>;
  using ImportPathRaw = llvm::ArrayRef<ImportPathElement>;

  template<typename Subclass>
  class ImportPathBase {
  public:
    using Element = ImportPathElement;
    using Raw = ImportPathRaw;

  protected:
    Raw raw;

    ImportPathBase(Raw raw) : raw(raw) { }

  public:
    const Raw &getRaw() const { return raw; }

    Raw::iterator begin() const {
      return raw.begin();
    }

    Raw::iterator end() const {
      return raw.end();
    }

    const Element &operator[](size_t i) const { return raw[i]; }
    bool empty() const { return raw.empty(); }
    size_t size() const { return raw.size(); }

    const Element &front() const { return raw.front(); }
    const Element &back() const { return raw.back(); }

    /// True if \c this and \c other are precisely equal, including SourceLocs.
    bool operator==(const Subclass &other) const {
      return raw == other.raw;
    }

    /// True if \c this and \c other contain the same identifiers in the same
    /// order, ignoring SourceLocs.
    bool isSameAs(const Subclass &other) const {
      return size() == other.size()
             && std::equal(this->begin(), this->end(), other.begin(),
                  [](const Element &l, const Element &r) -> bool {
                    return l.Item == r.Item;
                  }
                );
    }

    Subclass getTopLevelPath() const {
      assert(size() >= 1 && "nothing to take");
      return Subclass(raw.take_front());
    }

    Subclass getParentPath() const {
      assert(size() >= 0 && "nothing to take");
      return Subclass(raw.drop_back());
    }

    SourceRange getSourceRange() const {
      if (empty()) return SourceRange();
      return SourceRange(raw.front().Loc, raw.back().Loc);
    }
  };

  // These shims avoid circularity between ASTContext.h and Import.h.
  ImportPathRaw ImportPathBuilder_copyToImpl(ASTContext &ctx,
                                             ImportPathRaw raw);
  Identifier ImportPathBuilder_getIdentifierImpl(ASTContext &ctx,
                                                 StringRef string);

  template<typename Subclass>
  class ImportPathBuilder {
    llvm::SmallVector<ImportPathElement, 4> scratch;

  public:
    Subclass get() const {
      return Subclass(scratch);
    }

    Subclass copyTo(ASTContext &ctx) const {
      return Subclass(ImportPathBuilder_copyToImpl(ctx, scratch));
    }

    ImportPathBuilder() : scratch() { }
    ImportPathBuilder(const ImportPathElement &elem) : scratch() {
      scratch = { elem };
    }
    ImportPathBuilder(Identifier name, SourceLoc loc = SourceLoc())
        : ImportPathBuilder(ImportPathElement(name, loc)) { }

    template<typename Iterator>
    ImportPathBuilder(Iterator begin, Iterator end) : scratch(begin, end) { }

    template<typename Range>
    ImportPathBuilder(Range collection)
        : scratch(collection.begin(), collection.end()) { }

    /// Parses \p text into elements separated by \p separator, with identifiers
    /// from \p ctx and invalid SourceLocs.
    ///
    /// \warning This is not very robust; for instance, it doesn't check the
    /// validity of the identifiers.
    ImportPathBuilder(ASTContext &ctx, StringRef text, char separator)
        : scratch()
    {
      while (!text.empty()) {
        StringRef next;
        std::tie(next, text) = text.split(separator);
        push_back(ImportPathBuilder_getIdentifierImpl(ctx, next));
      }
    }

    void push_back(const ImportPathElement &elem) { scratch.push_back(elem); }
    void push_back(Identifier name, SourceLoc loc = SourceLoc()) {
      scratch.push_back({ name, loc });
    }

    void pop_back() { scratch.pop_back(); }

    bool empty() const { return scratch.empty(); }
    size_t size() const { return scratch.size(); }

    llvm::SmallVector<ImportPathElement, 4>::iterator begin() {
      return scratch.begin();
    }
    llvm::SmallVector<ImportPathElement, 4>::iterator end() {
      return scratch.end();
    }

    const ImportPathElement &front() const { return scratch.front(); }
    ImportPathElement &front() { return scratch.front(); }
    const ImportPathElement &back() const { return scratch.back(); }
    ImportPathElement &back() { return scratch.back(); }

    template<typename Iterator>
    void append(Iterator begin, Iterator end) {
      scratch.append(begin, end);
    }

    template<typename Range>
    void append(Range collection) {
      append(collection.begin(), collection.end());
    }
  };
}

/// @name ImportPathBase Comparison Operators
/// @{
template <typename Subclass>
inline bool operator<(const detail::ImportPathBase<Subclass> &LHS,
                      const detail::ImportPathBase<Subclass> &RHS) {
  using Element = typename detail::ImportPathBase<Subclass>::Element;
  auto Comparator = [](const Element &l, const Element &r) {
    return l.Item.compare(r.Item) < 0;
  };
  return std::lexicographical_compare(LHS.begin(), LHS.end(), RHS.begin(),
                                      RHS.end(), Comparator);
}
/// @}

/// An undifferentiated series of dotted identifiers in an \c import statement,
/// like \c Foo.Bar. Each identifier is packaged with its corresponding source
/// location.
///
/// The first element of an \c ImportPath is always a top-level module name. The
/// remaining elements could specify a scope (naming a declaration in the
/// module) or a chain of submodule names. \c ImportPath does not differentiate
/// between these cases; its \c getModule() and \c getAccess() methods take an
/// \c ImportKind parameter to decide how to divvy up these identifiers.
///
/// \c ImportPath is only used when analyzing the parsed representation of code.
/// Most code should use \c ImportPath::Module or \c ImportPath::Access, which
/// have semantic meaning.
///
/// \c ImportPath is essentially a wrapper around \c ArrayRef and does not own
/// its elements, so something else needs to manage their lifetime.
/// \c ImportDecl owns the memory backing \c ImportDecl::getImportPath().
class ImportPath : public detail::ImportPathBase<ImportPath> {
public:
  /// A single dotted name from an \c ImportPath, \c ImportPath::Module, or
  /// \c ImportPath::Access, with its source location.
  using Element = detail::ImportPathBase<ImportPath>::Element;

  /// The backing type for \c ImportPath, \c ImportPath::Module, and
  /// \c ImportPath::Access; namely, an \c ArrayRef of \c ImportPath::Elements.
  using Raw = detail::ImportPathBase<ImportPath>::Raw;

  /// A helper type which encapsulates a temporary vector and can produce an
  /// import path from it. In addition to the obvious use in a temporary
  /// variable, this type can be used mid-expression to produce an import path
  /// that is valid until the end of the expression.
  using Builder = detail::ImportPathBuilder<ImportPath>;

  /// Represents an access path--the portion of an \c ImportPath which describes
  /// the name of a declaration to scope the import to.
  ///
  /// \c ImportPath::Access is used in scoped imports to designate a specific
  /// declaration inside the module. The import will only* cover this
  /// declaration, and will import it with a higher "priority" than usual, so
  /// name lookup will prefer it over identically-named declarations visible
  /// through other imports.
  ///
  /// (* Not actually only--e.g. extensions will be imported too. The primary
  /// use case for scoped imports is actually to resolve name conflicts, not to
  /// reduce the set of visible declarations.)
  ///
  /// When \c ImportPath::Access is empty, this means the import covers all
  /// declarations in the module.
  ///
  /// Although in theory Swift could support scoped imports of nested
  /// declarations, in practice it currently only supports scoped imports of
  /// top-level declarations. Reflecting this, \c ImportPath::Access is backed
  /// by an \c ArrayRef, but it asserts that the access path has zero or one
  /// elements.
  ///
  /// \c ImportPath::Access is essentially a wrapper around \c ArrayRef and does
  /// not own its elements, so something else needs to manage their lifetime.
  /// \c ImportDecl owns the memory backing \c ImportDecl::getAccessPath().
  class Access : public detail::ImportPathBase<Access> {
  public:
    /// A helper type which encapsulates a temporary vector and can produce a
    /// scope path from it. In addition to the obvious use in a temporary
    /// variable, this type can be used mid-expression to produce a scope path
    /// that is valid until the end of the expression.
    using Builder = detail::ImportPathBuilder<Access>;

    Access(ImportPath::Raw raw) : ImportPathBase(raw) {
      assert(size() <= 1 && "nested scoped imports are not supported");
    }

    Access() : ImportPathBase({}) { }

    /// Returns \c true if the scope of this import includes \c name. An empty
    /// scope matches all names.
    bool matches(DeclName name) const {
      return empty() || DeclName(front().Item).matchesRef(name);
    }
  };

  /// Represents a module path--the portion of an \c ImportPath which describes
  /// the name of the module being imported, possibly including submodules.
  ///
  /// \c ImportPath::Module contains one or more identifiers. The first
  /// identiifer names a top-level module. The second and subsequent
  /// identifiers, if present, chain together to name a specific submodule to
  /// import. (Although Swift modules cannot currently contain submodules, Swift
  /// can import Clang submodules.)
  ///
  /// \c ImportPath::Module is essentially a wrapper around \c ArrayRef and
  /// does not own its elements, so something else needs to manage their
  /// lifetime. \c ImportDecl owns the memory backing
  /// \c ImportDecl::getModulePath().
  class Module : public detail::ImportPathBase<Module> {
  public:
    /// A helper type which encapsulates a temporary vector and can produce a
    /// module path from it. In addition to the obvious use in a temporary
    /// variable, this type can be used mid-expression to produce a module path
    /// that is valid until the end of the expression.
    using Builder = detail::ImportPathBuilder<Module>;

    Module(ImportPath::Raw raw) : ImportPathBase(raw) {
      assert(size() >= 1 && "must have a top-level module");
    }

    // Note: This type does not have a constructor which just takes an
    // `Identifier` because it would not be able to create a temporary
    // `ImportPath::Element` with a long enough lifetime to return. Use
    // `ImportPath::Module::Builder` to create a temporary module path.

    bool hasSubmodule() const {
      return size() != 1;
    }

    ImportPath::Raw getSubmodulePath() const {
      return getRaw().drop_front();
    }
  };

  ImportPath(Raw raw) : ImportPathBase(raw) {
    assert(raw.size() >= 1 && "ImportPath must contain a module name");
  }

  /// Extracts the portion of the \c ImportPath which represents a module name,
  /// including submodules if appropriate.
  Module getModulePath(bool isScoped) const {
    if (isScoped)
      return Module(getRaw().drop_back());

    return Module(getRaw());
  }

  /// Extracts the portion of the \c ImportPath which represents a scope for the
  /// import.
  Access getAccessPath(bool isScoped) const {
    if (isScoped) {
      assert(size() >= 2 && "scoped ImportPath must contain a decl name");
      return Access(getRaw().take_back());
    }

    return Access();
  }

  /// Extracts the portion of the \c ImportPath which represents a module name,
  /// including submodules, assuming the \c ImportDecl has the indicated
  /// \c importKind.
  Module getModulePath(ImportKind importKind) const {
    return getModulePath(importKind != ImportKind::Module);
  }

  /// Extracts the portion of the \c ImportPath which represents a scope for the
  /// import, assuming the \c ImportDecl has the indicated \c importKind.
  Access getAccessPath(ImportKind importKind) const {
    return getAccessPath(importKind != ImportKind::Module);
  }
};

// MARK: - Abstractions of imports

/// Convenience struct to keep track of a module along with its access path.
struct alignas(uint64_t) ImportedModule {
  /// The access path from an import: `import Foo.Bar` -> `Foo.Bar`.
  ImportPath::Access accessPath;
  /// The actual module corresponding to the import.
  ///
  /// Invariant: The pointer is non-null.
  ModuleDecl *importedModule;

  ImportedModule(ImportPath::Access accessPath,
                 ModuleDecl *importedModule)
      : accessPath(accessPath), importedModule(importedModule) {
    assert(this->importedModule);
  }

  bool operator==(const ImportedModule &other) const {
    return (this->importedModule == other.importedModule) &&
           (this->accessPath == other.accessPath);
  }

  /// Uniques the items in \p imports, ignoring the source locations of the
  /// access paths.
  ///
  /// The order of items in \p imports is \e not preserved.
  static void removeDuplicates(SmallVectorImpl<ImportedModule> &imports);

  /// Arbitrarily orders ImportedModule records, for inclusion in sets and such.
  class Order {
  public:
    bool operator()(const ImportedModule &lhs,
                    const ImportedModule &rhs) const {
      if (lhs.importedModule != rhs.importedModule)
        return std::less<const ModuleDecl *>()(lhs.importedModule,
                                               rhs.importedModule);
      if (lhs.accessPath.getRaw().data() != rhs.accessPath.getRaw().data())
        return std::less<ImportPath::Raw::iterator>()(lhs.accessPath.begin(),
                                                   rhs.accessPath.begin());
      return lhs.accessPath.size() < rhs.accessPath.size();
    }
  };
};

template<class ModuleInfo>
struct AttributedImport {
  ModuleInfo module;

  ImportOptions importOptions;

  // Filename for a @_private import.
  StringRef filename;

  // Names of explicitly imported SPIs.
  ArrayRef<Identifier> spiGroups;

  AttributedImport(ModuleInfo module, ImportOptions options,
                   StringRef filename = {}, ArrayRef<Identifier> spiGroups = {})
      : module(module), importOptions(options), filename(filename),
        spiGroups(spiGroups) {
    assert(!(importOptions.contains(ImportFlags::Exported) &&
             importOptions.contains(ImportFlags::ImplementationOnly)) ||
           importOptions.contains(ImportFlags::Reserved));
  }
};

using ImportedModuleDesc = AttributedImport<ImportedModule>;

// MARK: - Implicit imports

/// A module which has been implicitly imported.
struct ImplicitImport {
  ModuleDecl *Module;
  ImportOptions Options;

  ImplicitImport(ModuleDecl *module, ImportOptions opts = {})
      : Module(module), Options(opts) {}

  friend bool operator==(const ImplicitImport &lhs,
                         const ImplicitImport &rhs) {
    return lhs.Module == rhs.Module &&
           lhs.Options.toRaw() == rhs.Options.toRaw();
  }
};

void simple_display(llvm::raw_ostream &out, const ImplicitImport &import);

/// The kind of stdlib that should be imported.
enum class ImplicitStdlibKind {
  /// No standard library should be implicitly imported.
  None,

  /// The Builtin module should be implicitly imported.
  Builtin,

  /// The regular Swift standard library should be implicitly imported.
  Stdlib
};

struct ImplicitImportInfo {
  /// The implicit stdlib to import.
  ImplicitStdlibKind StdlibKind;

  /// Whether we should attempt to import an underlying Clang half of this
  /// module.
  bool ShouldImportUnderlyingModule;

  /// The bridging header path for this module, empty if there is none.
  StringRef BridgingHeaderPath;

  /// The names of additional modules to be implicitly imported.
  SmallVector<Identifier, 4> ModuleNames;

  /// An additional list of already-loaded modules which should be implicitly
  /// imported.
  SmallVector<std::pair<ModuleDecl *, /*exported*/ bool>, 4>
      AdditionalModules;

  ImplicitImportInfo()
      : StdlibKind(ImplicitStdlibKind::None),
        ShouldImportUnderlyingModule(false) {}
};

}

// MARK: - DenseMapInfo

namespace llvm {

template<>
struct DenseMapInfo<swift::ImportOptions> {
  using ImportOptions = swift::ImportOptions;

  using UnsignedDMI = DenseMapInfo<uint8_t>;

  static inline ImportOptions getEmptyKey() {
    return ImportOptions(UnsignedDMI::getEmptyKey());
  }
  static inline ImportOptions getTombstoneKey() {
    return ImportOptions(UnsignedDMI::getTombstoneKey());
  }
  static inline unsigned getHashValue(ImportOptions options) {
    return UnsignedDMI::getHashValue(options.toRaw());
  }
  static bool isEqual(ImportOptions a, ImportOptions b) {
    return UnsignedDMI::isEqual(a.toRaw(), b.toRaw());
  }
};

template <>
class DenseMapInfo<swift::ImportedModule> {
  using ImportedModule = swift::ImportedModule;
  using ModuleDecl = swift::ModuleDecl;
public:
  static ImportedModule getEmptyKey() {
    return {{}, llvm::DenseMapInfo<ModuleDecl *>::getEmptyKey()};
  }
  static ImportedModule getTombstoneKey() {
    return {{}, llvm::DenseMapInfo<ModuleDecl *>::getTombstoneKey()};
  }

  static unsigned getHashValue(const ImportedModule &val) {
    auto pair = std::make_pair(val.accessPath.size(), val.importedModule);
    return llvm::DenseMapInfo<decltype(pair)>::getHashValue(pair);
  }

  static bool isEqual(const ImportedModule &lhs,
                      const ImportedModule &rhs) {
    return lhs.importedModule == rhs.importedModule &&
           lhs.accessPath.isSameAs(rhs.accessPath);
  }
};

template<typename ModuleInfo>
struct DenseMapInfo<swift::AttributedImport<ModuleInfo>> {
  using AttributedImport = swift::AttributedImport<ModuleInfo>;

  using ModuleInfoDMI = DenseMapInfo<ModuleInfo>;
  using ImportOptionsDMI = DenseMapInfo<swift::ImportOptions>;
  using StringRefDMI = DenseMapInfo<StringRef>;
  // FIXME: SPI groups not used by DenseMapInfo???

  static inline AttributedImport getEmptyKey() {
    return AttributedImport(ModuleInfoDMI::getEmptyKey(),
                            ImportOptionsDMI::getEmptyKey(),
                            StringRefDMI::getEmptyKey());
  }
  static inline AttributedImport getTombstoneKey() {
    return AttributedImport(ModuleInfoDMI::getTombstoneKey(),
                            ImportOptionsDMI::getTombstoneKey(),
                            StringRefDMI::getTombstoneKey());
  }
  static inline unsigned getHashValue(const AttributedImport &import) {
    return detail::combineHashValue(
        ModuleInfoDMI::getHashValue(import.module),
        detail::combineHashValue(
            ImportOptionsDMI::getHashValue(import.importOptions),
            StringRefDMI::getHashValue(import.filename)));
  }
  static bool isEqual(const AttributedImport &a,
                      const AttributedImport &b) {
    return ModuleInfoDMI::isEqual(a.module, b.module) &&
           ImportOptionsDMI::isEqual(a.importOptions, b.importOptions) &&
           StringRefDMI::isEqual(a.filename, b.filename);
  }
};
}

#endif
