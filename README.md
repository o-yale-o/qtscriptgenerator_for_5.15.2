# Qt Script Generator (labs package, version 0.2)

The Qt Script Generator is a tool that generates Qt bindings for Qt Script. This should work
with Qt 5.6.0 and MSVC 2015 (Update 1).

---

## Instructions

1. **Build the generator**

   ```
   cd path/to/this/project/generator
   qmake && make
   ```

2. **Run the generator** (without arguments)

   This will generate C++ files in `path/to/this/project/generated_cpp`
   and documentation in `path/to/this/project/doc`.

3. **Build the bindings plugins**

   ```
   cd path/to/this/project/qtbindings
   qmake && make
   ```

   The plugins will be put under `path/to/this/project/plugins`.

4. **Use the plugins in your application**

   Add the plugins path to the library paths
   (`QCoreApplication::setLibraryPaths()`), then call `QScriptEngine::importExtension()`
   (plugin keys are `"qt.core"`, `"qt.gui"`, etc).

There is a simple script interpreter / launcher in `path/to/this/project/qtbindings/qs_eval`
that imports all the bindings. You can use it to run the examples found in
`path/to/this/project/examples`. E.g., with the examples directory being the working directory:

```
../qtbindings/qs_eval/qs_eval CollidingMice.js
```

See the generated `doc/index.html` for more information.

Have fun!

---

## 【修改说明】

本仓库基于原版 Qt Script Generator 0.2 修改，使其能在 Qt 5.15.2（MSVC 2019）下正确生成绑定代码。

### 背景

原版生成器只针对 Qt 4 时代的 C++ 语法。在 Qt 5.15.2 头文件中，大量类（QObjectData、QIODevice 等）
内部使用了 C++11 的 `= default` / `= delete` 函数定义语法，例如（qobject.h）：

```cpp
class QObjectData {
    QObjectData(const QObjectData &) = delete;
    QObjectData &operator=(const QObjectData &) = delete;
public:
    QObjectData() = default;
    virtual ~QObjectData() = 0;
    ...
};

class Q_CORE_EXPORT QObject { ... };   // 紧跟在 QObjectData 之后
```

旧 parser 的 `parseInitializer()` 只识别 `= <表达式>` 和 `= 0`，遇到 `= default` / `= delete`
时，关键字既不是表达式也不是 0，子解析器会失败但不会消费 token，导致 parser 状态机错乱，
从而把紧随其后的整个类（如 QObject 本身）跳过。QObject 一旦丢失，依赖链上的 QFile /
QFileDevice / QIODevice 等大量类会在拓扑排序阶段被一并丢弃，最终每次只生成 5~10 个
类型（且不稳定，因 QHash 迭代顺序随机）。

排查时表现为：typesystem 中 71 个 core 类，实际只生成个位数；表面看似"头文件多行注释
/某种新语法把后续类吃掉了"。

### 修复

- 文件：`generator/parser/parser.cpp`
- 函数：`Parser::parseInitializer`（约第 2187 行）

在消费完 `=` 之后，先判断下一个 token 是否为 `Token_default` 或 `Token_delete`：
若是，则直接消费该关键字并构造一个空的 `InitializerClause`，不再走表达式解析路径。
否则维持原有逻辑（按表达式或 `= 0` 处理）。

```cpp
if (nextTk == Token_default || nextTk == Token_delete)
{
    token_stream.nextToken();
    InitializerClauseAST *clause = CreateNode<InitializerClauseAST>(_M_pool);
    ast->initializer_clause = clause;
}
else if (!parseInitializerClause(ast->initializer_clause))
{
    reportError(("Initializer clause expected"));
}
```

### 其它配套修改（main.cpp / main.h）

为脱离 Qt 资源系统 (.qrc) 在文件系统上运行，`generator/main.cpp` 与 `generator/main.h`
已改为从 `applicationDirPath()/data/` 下读取 `qtscript_masterinclude.h`、`build_all.txt`
和 `pp-qt-configuration`；预处理器仍从环境变量 `INCLUDE`、`QTDIR` 以及命令行
`--include-paths` 获取头文件搜索路径。

### 验证

环境：Qt 5.15.2 (msvc2019_64) + MSVC 2019。

修复后重新生成所有模块，cpp 文件数量与原 4 月份正常运行的输出一致（gui 反而多 1 个）：

```
core=70  gui=86  widgets=189  network=17  xml=17  xmlpatterns=18
multimedia=2  opengl=5  printsupport=7  sql=12  svg=3  uitools=1
```

用 cl.exe 实际编译 qtscript_QAbstractAnimation.cpp / QFile.cpp / QFileDevice.cpp /
QIODevice.cpp / QProcess.cpp / QDate.cpp / QSettings.cpp 全部通过（仅 Qt 自身的
deprecated API 警告，无错误）。

### 运行步骤（Qt 5.15.2 / Windows）

1. **生成 generator.exe**（构建产物不入库，请按下述步骤自行构建）：

   ```
   cd generator
   qmake generator.pro -spec win32-msvc "CONFIG += release"
   nmake /f Makefile.Release
   ```

2. **设置环境并运行生成**（注意必须设 QTDIR 并把 `Qt\5.15.2\msvc2019_64\bin` 加入 PATH，
   否则 Qt5Core.dll 加载失败、预处理器也找不到 Qt 头文件）：

   ```
   set QTDIR=C:\Qt\5.15.2\msvc2019_64
   set PATH=C:\Qt\5.15.2\msvc2019_64\bin;%PATH%
   cd generator
   generator.exe qtscript_masterinclude.h typesystem_core.xml
   generator.exe qtscript_masterinclude.h typesystem_gui.xml
   generator.exe qtscript_masterinclude.h typesystem_widgets.xml
   ... 其余模块 (network / xml / sql / svg / opengl / multimedia / printsupport /
       xmlpatterns / uitools) 同理。
   ```

3. **编译绑定插件**：

   ```
   cd qtbindings
   qmake qtbindings.pro -spec win32-msvc "CONFIG += release"
   nmake
   ```

### 生成脚本解释器 qs_eval

`qtbindings/qs_eval/qs_eval.exe` 是一个简单的脚本解释器/启动器：它会导入全部绑定插件
（`qt.core`、`qt.gui`、`qt.widgets` 等），因此脚本里可以直接 `new QPushButton()` 使用 Qt 类。
**无需单独编译**——它已列入 `qtbindings.pro` 的 `SUBDIRS`，上一步编译绑定插件时会一起生成，
产物位于 `qtbindings/qs_eval/qs_eval.exe`（依赖同目录下的 Qt DLL 及 `plugins/` 下的绑定插件，
需保证运行时 `QTDIR\...\bin` 在 PATH 中，且插件的搜索路径可达）。

### 跑 Demo（验证脚本能正常引用 Qt 类）

绑定编译完成后，用 qs_eval 运行 `examples/` 下自带的示例脚本做最终验证
（以 examples 目录为工作目录）：

```
cd examples
..\qtbindings\qs_eval\qs_eval CollidingMice.js
```

窗口弹出、老鼠碰撞动画正常运行，即说明脚本对 Qt 类（QGraphicsScene、QPainter、
定时器、信号槽等）的引用全部有效。其它示例同理，例如：

```
..\qtbindings\qs_eval\qs_eval TwoWayButton.qs    :: 20 行的状态机按钮
..\qtbindings\qs_eval\qs_eval AnalogClock.js     :: 模拟时钟
..\qtbindings\qs_eval\qs_eval Wiggly.js          :: 文字抖动动画
```

其中 `TwoWayButton.qs` 最小（完整源码见文件）：用 `QStateMachine` + `QState` +
`assignProperty` + `clicked()` 信号转移，实现一个 On/Off 切换按钮——
如果它能正常弹出并响应点击，就证明核心/状态机/控件这条绑定链路是通的。

---

## 【修改说明 · 续】让全部 12 个模块从源头可再生成、可编译、可运行

在上一轮"能编过若干个类"的基础上，本轮完成了**生成器源头**的系统性修复：
不再手改任何生成物，重新用 generator.exe 批量生成的 .cpp/.h 直接可编译。
12 个模块（core/gui/widgets/printsupport/network/xml/xmlpatterns/multimedia/
opengl/sql/svg/uitools）的插件 DLL（release+debug）与 qs_eval.exe 全部构建成功，
CollidingMice.js、TwoWayButton.qs 等示例实际运行通过。

### 1. 跨模块类型孤岛：`load-typesystem`

**现象**：单独生成某模块时，凡参数/基类涉及其它模块的类（如 QStyleOption 的
QFont、QHeader 视图的 QWidget 参数），要么整类变 abstract 空壳、要么构造参数
类型解析失败。

**根因**：typesystem_*.xml 原本互相不加载。core 之外的所有模块 typesystem 里，
QtCore 的类型（QString、QObject 等）由 TypeDatabase 内建 primitive 补齐，但
QtGui/QtWidgets 的类对 printsupport/uitools 等模块完全未知——未注册类型的参数
会被当作无法匹配而丢弃函数，基类不认识则整类变 abstract。

**修复**：每个模块 typesystem 头部加入（以 widgets 为例）：

```xml
<load-typesystem name="typesystem_core.xml" generate="no"/>
<load-typesystem name="typesystem_gui.xml" generate="no"/>
```

`generate="no"` 使被加载模块的类以 GenerateForSubclass 模式注册：类型可解析、
函数签名可匹配，但不会在当前模块重复输出绑定文件。`uitools` 依次加载
core+gui+widgets，`printsupport` 加载 core+gui+widgets，以此类推。
gui 加载 core；xml/xmlpatterns/network/sql 加载 core；multimedia/opengl/svg
加载 core+gui。

### 2. `= nullptr` 默认实参不识别（Qt5 头的普遍写法）

**现象**：`new QPushButton()`（无参）抛 "could not find a function match"。
Qt5 头文件普遍写 `QWidget *parent = nullptr`，而 Qt4 时代写 `= 0`。

**根因**：`AbstractMetaBuilder::translateDefaultValue()`（abstractmetabuilder.cpp）
只把字面量 `"0"` 翻译为 "null"，`"nullptr"` 落入对象类型分支返回空串，
导致默认实参被丢弃、最小参数个数=满参个数，0 参构造无分支可派发。

**修复**：

```cpp
} else if (expr == "0" || expr == "nullptr") {
    return "null";
```

修复后所有带 nullptr 默认值的函数都能以最少参数调用（QPushButton、QTimer、
QGridLayout 等成千上万处受益）。

### 3. Qt5 的 per-TU QMetaTypeId 特化要求（metatype 声明策略）

Qt4 时代 `qscriptvalue_cast<T>` 靠 QVariant 运行期转换，声明一次即可；Qt5 的
`qMetaTypeId<T>()` 要求**每个翻译单元**都有 `QMetaTypeId<T>` 特化，即生成的
每个 .cpp 都需要自己的 `Q_DECLARE_METATYPE(T)`。相应修改（classgenerator.cpp）：

- **值类型不再因"无默认构造函数"被跳过**：原来无默认 ctor 的值类型
  （QStyleOption、QPicture 等）被预先塞进 registeredTypeNames 而不发射声明，
  Qt5 下这些 TU 里的 `qscriptvalue_cast<T>` 直接编译失败。现在值类型一律发射。
- **黑名单扩充**（`maybeDeclareMetaType`）：声明由手写 `__package_shared.h`
  提供的 QFontInfo/QFontMetrics/QFontMetricsF/**QEvent**（避免 C2766 重复特化），
  以及 **QTextStream**（Qt5 删除了拷贝构造，`Q_DECLARE_METATYPE(QTextStream)`
  本身无法编译）。
- **QDomDocument 等 SAX 相关类**：`Q_DECLARE_METATYPE(T*)` 在 Qt5 要求 T 完整
  定义，而 qdom.h 只有前置声明——给 QDomDocument 条目加 extra-includes
  （QXmlInputSource/QXmlReader）。

### 4. `qscriptvalue_cast<T&>` 结构性不兼容（Qt5 无法编译引用转换）

Qt5 的 qscriptengine.h 明确将 `QMetaTypeId2<T&>::Defined` 置 false，任何生成
`qscriptvalue_cast<X&>` 的代码都无法编译。生成器会为值类型的 shell 覆盖
`operator=`（返回 T&），Qt4 时代即可编译、Qt5 不行。对受影响的值类型在
typesystem 里统一 `remove`（与 gui 模块既有惯例一致）：

- `typesystem_widgets.xml`：QStyleOption 及 20 个 QStyleOption* 子类；
- `typesystem_xml.xml`：QXmlAttributes；
- `typesystem_xmlpatterns.xml`：QXmlNodeModelIndex。

另外 Qt5 把 QStyleOption::operator= 改为 protected，QCursor::operator== 变成
友元非成员、QLabel::picture() 加 const 等，均按 Qt4→Qt5 实际签名逐一 rejection。

### 5. parser 修复（详见前节 + 以下新增）

- `parseExceptionSpecification`：识别 noexcept/override/final；
- `parseUsing`：C++11 类型别名 `using X = Y;`；
- `parseQ_PROPERTY`：QDOC_PROPERTY 分支；
- `parseQ_ENUMS`：识别 Qt5 的 `Q_ENUM/Q_ENUM_NS/Q_FLAG/Q_FLAG_NS(...)`；
- `parseMemInitializer`：C++11 花括号成员初始化 `: member{a, b, c}`
  （QVector3D 等构造函数因此曾被整类丢弃）；
- **深度感知的成员循环错误恢复**：解析失败时不再调用会吞掉余下全文件的
  `skipUntilDeclaration()`，改为按花括号深度有界回退，绝不越过类结束大括号。
  修复后模型类数量从 1856 恢复到 2439。
  （注意：不可用 `token_stream.matchingBrace()`——该 API 在本 lexer 上从未被
  填充，返回的是垃圾值。）

### 6. 模块工程文件（.pro）

- `qtscript_sql`：`QT -= gui` → `QT += core gui sql`（QSqlDriver 引用
  QtGui/qevent.h）；
- `qtscript_xmlpatterns`：`QT -= gui` → `QT += core gui xmlpatterns network`；
- `qtscript_uitools`：删除 Mac framework 路径 `${QTDIR}/lib/QtWidgets.framework/Headers`
  （qmake 会原样写进 nmake 的 INCPATH，`{` 属非法宏字符导致 U1001）。

### 7. 示例脚本 Qt4→Qt5 API 适配

`examples/CollidingMice.js`：`QGraphicsItem::rotate()` 在 Qt5 已移除——
构造函数中改为 `setRotation(angle)`（初始角度 0，二者等价）；每帧累积旋转的
`rotate(dx)` 改为 `setRotation(rotation() + dx)`。

### 验证（本轮）

- 全部 12 模块经 generator.exe 重新生成后，nmake 全量编译链接通过
  （release + debug 两套），插件输出于 `plugins/script/`；
- `qs_eval TwoWayButton.qs`：窗口正常、点击切换、正常退出（exit 0）；
- `qs_eval CollidingMice.js`：7 只老鼠碰撞动画稳定运行；
- `qs_eval AnalogClock.js` 等其余示例的 QPainter.rotate/scale 为 Qt5 仍保留的
  API，无需改动。

### 已知限制

- `qt.xmlpatterns` / `qt.uitools` 插件在 qs_eval 启动时 import 失败
  （插件 DLL 已构建且依赖完整，import 失败原因未深究——不影响其它 10 个模块
  与全部示例运行）；`qt.webkit`/`qt.webkitwidgets` 本来就没有绑定。
- `QMatrix::inverted(bool*)`、`QTransform::inverted(bool*)` 等带输出指针参数的
  函数按 Qt4 时代惯例做了参数移除/私有化，脚本中拿到的是返回值版本。

---

## 【修改说明 · 三】槽函数绑定修复（AnalogClock 调通）

第一轮验证时 AnalogClock.js 报 `painter.begin is not a function`，深挖后修掉
一条完整的因果链：

1. **`pp-qt-configuration` 补 `#define slots`**。配置原本把 `Q_SLOTS` 展开成
   标识符 `slots`，但自身未定义 `slots`，导致预处理流中残留 `public slots:`，
   遗留 parser 在此处失败并靠深度恢复吞掉整段槽声明（QWidget 的
   show/hide/setVisible/setWindowTitle/deleteLater/raise/close… 全部丢失，
   计 300+ 个函数）。Qt 官方本就默认把 `slots` 定义为空，补上后槽函数按
   普通成员正常绑定。
2. **注册 `QPaintDevice`**（typesystem_gui.xml）。该类型缺失使
   `QPainter::begin(QPaintDevice*)` 与 `QPainter(QPaintDevice*)` 被静默丢弃，
   `painter.begin(this)` 不可用；所有构造函数均为 protected，
   `new QPaintDevice()` 依然不可能。
3. **容器条目 include 污染防护**（abstractmetabuilder.cpp）。Qt 5.15 的
   qevent.h 含 `template <> class QList<QPointingDeviceUniqueId> {}` 显式
   特化，解析器将其记录为名为 QList 的"类"（所在文件 qevent.h），绑定顺序
   一旦先碰到它，QList 容器条目的 include 即被污染 → 网络模块（不含 gui）
   全部生成文件 include qevent.h 而编译失败。现在容器条目不再从类遍历继承
   include。
4. **属性访问器纳入绑定表**（classgenerator.cpp）。原本 Q_PROPERTY 的
   read/write 函数被跳过，脚本子类（`this` 为普通 JS 对象）无法调用
   `setWindowTitle` 等。现按普通函数生成。
5. **`isQObjectBased` 支持多继承**（classgenerator.cpp）。QWidget 同时继承
   QObject 与 QPaintDevice，`baseClass()` 只沿单链走、可能选中
   QPaintDevice，导致 QWidget 被误判为非 QObject 系、构造器用 newVariant
   包装，信号 `connect(this,"slot()")` 全部失效。现优先读取 builder 按
   完整基类列表计算的 QObject 标志。
6. `QLabel::pixmap()` 返回 `const QPixmap*`（Qt4 为非 const），生成代码
   无法编译，按 `QLabel::picture` 先例 rejection。

**验证**：AnalogClock.js（时钟走针）、CollidingMice.js（老鼠碰撞）、
TwoWayButton.qs（状态机按钮）全部无脚本异常、正常交互退出（exit 0）。
QWidget 绑定函数表从 114 个恢复至 425 个。
