#pragma once
#include <functional>
#include <vector>
#include <string>
namespace hiro {
  struct Widget { void setVisible(bool) {} void setEnabled(bool) {} };
  struct Window : Widget {
    void setTitle(const std::string&) {} void setSize(const std::vector<int>&) {}
    void setAlignment(const std::string&) {} void setVisible(bool) {}
    void onDismiss(std::function<void()>) {}
  };
  struct Font {}; struct Color {}; struct Size { Size(int, int) {} };
  struct Alignment { Alignment(int, int) {} }; struct Image {};
  namespace Icon { namespace Emblem { inline Image Folder() { return {}; } } }
  struct ListViewItem {}; struct ListView : Widget { void append(ListViewItem*) {} };
  struct Canvas : Widget { void setSize(const std::vector<int>&) {} void update() {} };
  struct Button : Widget { void setText(const std::string&) {} void onActivate(std::function<void()>) {} };
  struct Label : Widget { void setText(const std::string&) {} };
  struct BrowserDialog {
    BrowserDialog& setTitle(const std::string&) { return *this; } BrowserDialog& setPath(const std::string&) { return *this; }
    BrowserDialog& setFilters(const std::vector<std::string>&) { return *this; }
    std::string openFile() { return ""; } std::string openFolder() { return ""; }
  };
  struct MessageDialog {
    MessageDialog& setTitle(const std::string&) { return *this; } MessageDialog& setText(const std::string&) { return *this; }
    void error() {} void warning() {} void information() {}
  };
  namespace Mouse { namespace Button { enum { Left }; } }
  inline int sx(int v) { return v; } inline int sy(int v) { return v; }
}
inline int operator""_sx(unsigned long long v) { return v; }
inline int operator""_sy(unsigned long long v) { return v; }
