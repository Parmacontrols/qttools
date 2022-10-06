// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "designerpropertymanager.h"
#include "qtpropertymanager.h"
#include "paletteeditorbutton.h"
#include "qlonglongvalidator.h"
#include "stringlisteditorbutton.h"
#include "qtresourceview_p.h"
#include "qtpropertybrowserutils_p.h"

#include <formwindowbase_p.h>
#include <formwindowmanager.h>
#include <formwindow.h>
#include <propertysheet.h>
#include <qextensionmanager.h>
#include <formwindowcursor.h>
#include <textpropertyeditor_p.h>
#include <stylesheeteditor_p.h>
#include <richtexteditor_p.h>
#include <plaintexteditor_p.h>
#include <iconloader_p.h>
#include <iconselector_p.h>
#include <abstractdialoggui_p.h>

#include <QtWidgets/qapplication.h>
#include <QtWidgets/qlabel.h>
#include <QtWidgets/qtoolbutton.h>
#include <QtWidgets/qboxlayout.h>
#include <QtWidgets/qlineedit.h>
#include <QtWidgets/qdialogbuttonbox.h>
#include <QtWidgets/qpushbutton.h>
#include <QtWidgets/qfiledialog.h>
#include <QtWidgets/qmenu.h>
#include <QtWidgets/qkeysequenceedit.h>

#include <QtGui/qaction.h>
#if QT_CONFIG(clipboard)
#include <QtGui/qclipboard.h>
#endif
#include <QtGui/qevent.h>

#include <QtCore/qdebug.h>
#include <QtCore/qfileinfo.h>
#include <QtCore/qurl.h>

QT_BEGIN_NAMESPACE

static const char *resettableAttributeC = "resettable";
static const char *flagsAttributeC = "flags";
static const char *validationModesAttributeC = "validationMode";
static const char *superPaletteAttributeC = "superPalette";
static const char *defaultResourceAttributeC = "defaultResource";
static const char *fontAttributeC = "font";
static const char *themeAttributeC = "theme";

class DesignerFlagPropertyType
{
};


class DesignerAlignmentPropertyType
{
};

QT_END_NAMESPACE

Q_DECLARE_METATYPE(DesignerFlagPropertyType)
Q_DECLARE_METATYPE(DesignerAlignmentPropertyType)

QT_BEGIN_NAMESPACE

namespace qdesigner_internal {

template <class PropertySheetValue>
void TranslatablePropertyManager<PropertySheetValue>::initialize(QtVariantPropertyManager *m,
                                                                 QtProperty *property,
                                                                 const PropertySheetValue &value)
{
    m_values.insert(property, value);

    QtVariantProperty *translatable = m->addProperty(QMetaType::Bool, DesignerPropertyManager::tr("translatable"));
    translatable->setValue(value.translatable());
    m_valueToTranslatable.insert(property, translatable);
    m_translatableToValue.insert(translatable, property);
    property->addSubProperty(translatable);

    if (!DesignerPropertyManager::useIdBasedTranslations()) {
        QtVariantProperty *disambiguation =
            m->addProperty(QMetaType::QString, DesignerPropertyManager::tr("disambiguation"));
        disambiguation->setValue(value.disambiguation());
        m_valueToDisambiguation.insert(property, disambiguation);
        m_disambiguationToValue.insert(disambiguation, property);
        property->addSubProperty(disambiguation);
    }

    QtVariantProperty *comment = m->addProperty(QMetaType::QString, DesignerPropertyManager::tr("comment"));
    comment->setValue(value.comment());
    m_valueToComment.insert(property, comment);
    m_commentToValue.insert(comment, property);
    property->addSubProperty(comment);

    if (DesignerPropertyManager::useIdBasedTranslations()) {
        QtVariantProperty *id = m->addProperty(QMetaType::QString, DesignerPropertyManager::tr("id"));
        id->setValue(value.id());
        m_valueToId.insert(property, id);
        m_idToValue.insert(id, property);
        property->addSubProperty(id);
    }
}

template <class PropertySheetValue>
bool TranslatablePropertyManager<PropertySheetValue>::uninitialize(QtProperty *property)
{
    if (QtProperty *comment = m_valueToComment.value(property)) {
        delete comment;
        m_commentToValue.remove(comment);
    } else {
        return false;
    }
    if (QtProperty *translatable = m_valueToTranslatable.value(property)) {
        delete translatable;
        m_translatableToValue.remove(translatable);
    }
    if (QtProperty *disambiguation = m_valueToDisambiguation.value(property)) {
        delete disambiguation;
        m_disambiguationToValue.remove(disambiguation);
    }
    if (QtProperty *id = m_valueToId.value(property)) {
        delete id;
        m_idToValue.remove(id);
    }

    m_values.remove(property);
    m_valueToComment.remove(property);
    m_valueToTranslatable.remove(property);
    m_valueToDisambiguation.remove(property);
    m_valueToId.remove(property);
    return true;
}

template <class PropertySheetValue>
bool TranslatablePropertyManager<PropertySheetValue>::destroy(QtProperty *subProperty)
{
    const auto commentToValueIt = m_commentToValue.find(subProperty);
    if (commentToValueIt != m_commentToValue.end()) {
        m_valueToComment.remove(commentToValueIt.value());
        m_commentToValue.erase(commentToValueIt);
        return true;
    }
    const auto translatableToValueIt = m_translatableToValue.find(subProperty);
    if (translatableToValueIt != m_translatableToValue.end()) {
        m_valueToTranslatable.remove(translatableToValueIt.value());
        m_translatableToValue.erase(translatableToValueIt);
        return true;
    }
    const auto disambiguationToValueIt = m_disambiguationToValue.find(subProperty);
    if (disambiguationToValueIt != m_disambiguationToValue.end()) {
        m_valueToDisambiguation.remove(disambiguationToValueIt.value());
        m_disambiguationToValue.erase(disambiguationToValueIt);
        return true;
    }
    const auto idToValueIt = m_idToValue.find(subProperty);
    if (idToValueIt != m_idToValue.end()) {
        m_valueToId.remove(idToValueIt.value());
        m_idToValue.erase(idToValueIt);
        return true;
    }
    return false;
}

template <class PropertySheetValue>
int TranslatablePropertyManager<PropertySheetValue>::valueChanged(QtVariantPropertyManager *m,
                                                                  QtProperty *propertyIn,
                                                                  const QVariant &value)
{
    if (QtProperty *property = m_translatableToValue.value(propertyIn, 0)) {
        const PropertySheetValue oldValue = m_values.value(property);
        PropertySheetValue newValue = oldValue;
        newValue.setTranslatable(value.toBool());
        if (newValue != oldValue) {
            m->variantProperty(property)->setValue(QVariant::fromValue(newValue));
            return DesignerPropertyManager::Changed;
        }
        return DesignerPropertyManager::Unchanged;
    }
    if (QtProperty *property = m_commentToValue.value(propertyIn)) {
        const PropertySheetValue oldValue = m_values.value(property);
        PropertySheetValue newValue = oldValue;
        newValue.setComment(value.toString());
        if (newValue != oldValue) {
            m->variantProperty(property)->setValue(QVariant::fromValue(newValue));
            return DesignerPropertyManager::Changed;
        }
        return DesignerPropertyManager::Unchanged;
    }
    if (QtProperty *property = m_disambiguationToValue.value(propertyIn, 0)) {
        const PropertySheetValue oldValue = m_values.value(property);
        PropertySheetValue newValue = oldValue;
        newValue.setDisambiguation(value.toString());
        if (newValue != oldValue) {
            m->variantProperty(property)->setValue(QVariant::fromValue(newValue));
            return DesignerPropertyManager::Changed;
        }
        return DesignerPropertyManager::Unchanged;
    }
    if (QtProperty *property = m_idToValue.value(propertyIn)) {
        const PropertySheetValue oldValue = m_values.value(property);
        PropertySheetValue newValue = oldValue;
        newValue.setId(value.toString());
        if (newValue != oldValue) {
            m->variantProperty(property)->setValue(QVariant::fromValue(newValue));
            return DesignerPropertyManager::Changed;
        }
        return DesignerPropertyManager::Unchanged;
    }
    return DesignerPropertyManager::NoMatch;
}

template <class PropertySheetValue>
int TranslatablePropertyManager<PropertySheetValue>::setValue(QtVariantPropertyManager *m,
                                                              QtProperty *property,
                                                              int expectedTypeId,
                                                              const QVariant &variantValue)
{
    const auto it = m_values.find(property);
    if (it == m_values.end())
        return DesignerPropertyManager::NoMatch;
    if (variantValue.userType() != expectedTypeId)
        return DesignerPropertyManager::NoMatch;
    const PropertySheetValue value = qvariant_cast<PropertySheetValue>(variantValue);
    if (value == it.value())
        return DesignerPropertyManager::Unchanged;
    if (QtVariantProperty *comment = m->variantProperty(m_valueToComment.value(property)))
        comment->setValue(value.comment());
    if (QtVariantProperty *translatable = m->variantProperty(m_valueToTranslatable.value(property)))
        translatable->setValue(value.translatable());
    if (QtVariantProperty *disambiguation = m->variantProperty(m_valueToDisambiguation.value(property)))
        disambiguation->setValue(value.disambiguation());
    if (QtVariantProperty *id = m->variantProperty(m_valueToId.value(property)))
        id->setValue(value.id());
    it.value() = value;
    return DesignerPropertyManager::Changed;
}

template <class PropertySheetValue>
bool TranslatablePropertyManager<PropertySheetValue>::value(const QtProperty *property, QVariant *rc) const
{
    const auto it = m_values.constFind(const_cast<QtProperty *>(property));
    if (it == m_values.constEnd())
        return false;
    *rc = QVariant::fromValue(it.value());
    return true;
}

// ------------ TextEditor
class TextEditor : public QWidget
{
    Q_OBJECT
public:
    TextEditor(QDesignerFormEditorInterface *core, QWidget *parent);

    TextPropertyValidationMode textPropertyValidationMode() const;
    void setTextPropertyValidationMode(TextPropertyValidationMode vm);

    void setRichTextDefaultFont(const QFont &font) { m_richTextDefaultFont = font; }
    QFont richTextDefaultFont() const { return m_richTextDefaultFont; }

    void setSpacing(int spacing);

    TextPropertyEditor::UpdateMode updateMode() const     { return m_editor->updateMode(); }
    void setUpdateMode(TextPropertyEditor::UpdateMode um) { m_editor->setUpdateMode(um); }

    void setIconThemeModeEnabled(bool enable);

public slots:
    void setText(const QString &text);

signals:
    void textChanged(const QString &text);

private slots:
    void buttonClicked();
    void resourceActionActivated();
    void fileActionActivated();
private:
    TextPropertyEditor *m_editor;
    IconThemeEditor *m_themeEditor;
    bool m_iconThemeModeEnabled;
    QFont m_richTextDefaultFont;
    QToolButton *m_button;
    QMenu *m_menu;
    QAction *m_resourceAction;
    QAction *m_fileAction;
    QHBoxLayout *m_layout;
    QDesignerFormEditorInterface *m_core;
};

TextEditor::TextEditor(QDesignerFormEditorInterface *core, QWidget *parent) :
    QWidget(parent),
    m_editor(new TextPropertyEditor(this)),
    m_themeEditor(new IconThemeEditor(this, false)),
    m_iconThemeModeEnabled(false),
    m_richTextDefaultFont(QApplication::font()),
    m_button(new QToolButton(this)),
    m_menu(new QMenu(this)),
    m_resourceAction(new QAction(tr("Choose Resource..."), this)),
    m_fileAction(new QAction(tr("Choose File..."), this)),
    m_layout(new QHBoxLayout(this)),
    m_core(core)
{
    m_themeEditor->setVisible(false);
    m_button->setVisible(false);

    m_layout->addWidget(m_editor);
    m_layout->addWidget(m_themeEditor);
    m_button->setText(tr("..."));
    m_button->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Ignored);
    m_button->setFixedWidth(20);
    m_layout->addWidget(m_button);
    m_layout->setContentsMargins(QMargins());
    m_layout->setSpacing(0);

    connect(m_resourceAction, &QAction::triggered, this, &TextEditor::resourceActionActivated);
    connect(m_fileAction, &QAction::triggered, this, &TextEditor::fileActionActivated);
    connect(m_editor, &TextPropertyEditor::textChanged, this, &TextEditor::textChanged);
    connect(m_themeEditor, &IconThemeEditor::edited, this, &TextEditor::textChanged);
    connect(m_button, &QAbstractButton::clicked, this, &TextEditor::buttonClicked);

    setSizePolicy(QSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed));
    setFocusProxy(m_editor);

    m_menu->addAction(m_resourceAction);
    m_menu->addAction(m_fileAction);
}

void TextEditor::setSpacing(int spacing)
{
    m_layout->setSpacing(spacing);
}

void TextEditor::setIconThemeModeEnabled(bool enable)
{
    if (m_iconThemeModeEnabled == enable)
        return; // nothing changes
    m_iconThemeModeEnabled = enable;
    m_editor->setVisible(!enable);
    m_themeEditor->setVisible(enable);
    if (enable) {
        m_themeEditor->setTheme(m_editor->text());
        setFocusProxy(m_themeEditor);
    } else {
        m_editor->setText(m_themeEditor->theme());
        setFocusProxy(m_editor);
    }
}

TextPropertyValidationMode TextEditor::textPropertyValidationMode() const
{
    return m_editor->textPropertyValidationMode();
}

void TextEditor::setTextPropertyValidationMode(TextPropertyValidationMode vm)
{
    m_editor->setTextPropertyValidationMode(vm);
    if (vm == ValidationURL) {
        m_button->setMenu(m_menu);
        m_button->setFixedWidth(30);
        m_button->setPopupMode(QToolButton::MenuButtonPopup);
    } else {
        m_button->setMenu(nullptr);
        m_button->setFixedWidth(20);
        m_button->setPopupMode(QToolButton::DelayedPopup);
    }
    m_button->setVisible(vm == ValidationStyleSheet || vm == ValidationRichText || vm == ValidationMultiLine || vm == ValidationURL);
}

void TextEditor::setText(const QString &text)
{
    if (m_iconThemeModeEnabled)
        m_themeEditor->setTheme(text);
    else
        m_editor->setText(text);
}

void TextEditor::buttonClicked()
{
    const QString oldText = m_editor->text();
    QString newText;
    switch (textPropertyValidationMode()) {
    case ValidationStyleSheet: {
        StyleSheetEditorDialog dlg(m_core, this);
        dlg.setText(oldText);
        if (dlg.exec() != QDialog::Accepted)
            return;
        newText = dlg.text();
    }
        break;
    case ValidationRichText: {
        RichTextEditorDialog dlg(m_core, this);
        dlg.setDefaultFont(m_richTextDefaultFont);
        dlg.setText(oldText);
        if (dlg.showDialog() != QDialog::Accepted)
            return;
        newText = dlg.text(Qt::AutoText);
    }
        break;
    case ValidationMultiLine: {
        PlainTextEditorDialog dlg(m_core, this);
        dlg.setDefaultFont(m_richTextDefaultFont);
        dlg.setText(oldText);
        if (dlg.showDialog() != QDialog::Accepted)
            return;
        newText = dlg.text();
    }
        break;
    case ValidationURL:
        if (oldText.isEmpty() || oldText.startsWith(QStringLiteral("qrc:")))
            resourceActionActivated();
        else
            fileActionActivated();
        return;
    default:
        return;
    }
    if (newText != oldText) {
        m_editor->setText(newText);
        emit textChanged(newText);
    }
}

void TextEditor::resourceActionActivated()
{
    QString oldPath = m_editor->text();
    if (oldPath.startsWith(QStringLiteral("qrc:")))
        oldPath.remove(0, 4);
    // returns ':/file'
    QString newPath = IconSelector::choosePixmapResource(m_core, m_core->resourceModel(), oldPath, this);
    if (newPath.startsWith(QLatin1Char(':')))
         newPath.remove(0, 1);
    if (newPath.isEmpty() || newPath == oldPath)
        return;
    const QString newText = QStringLiteral("qrc:") + newPath;
    m_editor->setText(newText);
    emit textChanged(newText);
}

void TextEditor::fileActionActivated()
{
    QString oldPath = m_editor->text();
    if (oldPath.startsWith(QStringLiteral("file:")))
        oldPath = oldPath.mid(5);
    const QString newPath = m_core->dialogGui()->getOpenFileName(this, tr("Choose a File"), oldPath);
    if (newPath.isEmpty() || newPath == oldPath)
        return;
    const QString newText = QUrl::fromLocalFile(newPath).toString();
    m_editor->setText(newText);
    emit textChanged(newText);
}

// ------------ ThemeInputDialog

class IconThemeDialog : public QDialog
{
    Q_OBJECT
public:
    static QString getTheme(QWidget *parent, const QString &theme, bool *ok);
private:
    IconThemeDialog(QWidget *parent);
    IconThemeEditor *m_editor;
};

IconThemeDialog::IconThemeDialog(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Set Icon From Theme"));

    QVBoxLayout *layout = new QVBoxLayout(this);
    QLabel *label = new QLabel(tr("Select icon name from theme:"), this);
    m_editor = new IconThemeEditor(this);
    QDialogButtonBox *buttons = new QDialogButtonBox(this);
    buttons->setStandardButtons(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);

    layout->addWidget(label);
    layout->addWidget(m_editor);
    layout->addWidget(buttons);

    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
}

QString IconThemeDialog::getTheme(QWidget *parent, const QString &theme, bool *ok)
{
    IconThemeDialog dlg(parent);
    dlg.m_editor->setTheme(theme);
    if (dlg.exec() == QDialog::Accepted) {
        *ok = true;
        return dlg.m_editor->theme();
    }
    *ok = false;
    return QString();
}

// ------------ PixmapEditor
class PixmapEditor : public QWidget
{
    Q_OBJECT
public:
    PixmapEditor(QDesignerFormEditorInterface *core, QWidget *parent);

    void setSpacing(int spacing);
    void setPixmapCache(DesignerPixmapCache *cache);
    void setIconThemeModeEnabled(bool enabled);
public slots:
    void setPath(const QString &path);
    void setTheme(const QString &theme);
    void setDefaultPixmap(const QPixmap &pixmap);

signals:
    void pathChanged(const QString &path);
    void themeChanged(const QString &theme);

protected:
    void contextMenuEvent(QContextMenuEvent *event) override;

private slots:
    void defaultActionActivated();
    void resourceActionActivated();
    void fileActionActivated();
    void themeActionActivated();
#if QT_CONFIG(clipboard)
    void copyActionActivated();
    void pasteActionActivated();
    void clipboardDataChanged();
#endif
private:
    void updateLabels();
    bool m_iconThemeModeEnabled;
    QDesignerFormEditorInterface *m_core;
    QLabel *m_pixmapLabel;
    QLabel *m_pathLabel;
    QToolButton *m_button;
    QAction *m_resourceAction;
    QAction *m_fileAction;
    QAction *m_themeAction;
    QAction *m_copyAction;
    QAction *m_pasteAction;
    QHBoxLayout *m_layout;
    QPixmap m_defaultPixmap;
    QString m_path;
    QString m_theme;
    DesignerPixmapCache *m_pixmapCache;
};

PixmapEditor::PixmapEditor(QDesignerFormEditorInterface *core, QWidget *parent) :
    QWidget(parent),
    m_iconThemeModeEnabled(false),
    m_core(core),
    m_pixmapLabel(new QLabel(this)),
    m_pathLabel(new QLabel(this)),
    m_button(new QToolButton(this)),
    m_resourceAction(new QAction(tr("Choose Resource..."), this)),
    m_fileAction(new QAction(tr("Choose File..."), this)),
    m_themeAction(new QAction(tr("Set Icon From Theme..."), this)),
    m_copyAction(new QAction(createIconSet(QStringLiteral("editcopy.png")), tr("Copy Path"), this)),
    m_pasteAction(new QAction(createIconSet(QStringLiteral("editpaste.png")), tr("Paste Path"), this)),
    m_layout(new QHBoxLayout(this)),
    m_pixmapCache(nullptr)
{
    m_layout->addWidget(m_pixmapLabel);
    m_layout->addWidget(m_pathLabel);
    m_button->setText(tr("..."));
    m_button->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Ignored);
    m_button->setFixedWidth(30);
    m_button->setPopupMode(QToolButton::MenuButtonPopup);
    m_layout->addWidget(m_button);
    m_layout->setContentsMargins(QMargins());
    m_layout->setSpacing(0);
    m_pixmapLabel->setFixedWidth(16);
    m_pixmapLabel->setAlignment(Qt::AlignCenter);
    m_pathLabel->setSizePolicy(QSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed));
    m_themeAction->setVisible(false);

    QMenu *menu = new QMenu(this);
    menu->addAction(m_resourceAction);
    menu->addAction(m_fileAction);
    menu->addAction(m_themeAction);

    m_button->setMenu(menu);
    m_button->setText(tr("..."));

    connect(m_button, &QAbstractButton::clicked, this, &PixmapEditor::defaultActionActivated);
    connect(m_resourceAction, &QAction::triggered, this, &PixmapEditor::resourceActionActivated);
    connect(m_fileAction, &QAction::triggered, this, &PixmapEditor::fileActionActivated);
    connect(m_themeAction, &QAction::triggered, this, &PixmapEditor::themeActionActivated);
#if QT_CONFIG(clipboard)
    connect(m_copyAction, &QAction::triggered, this, &PixmapEditor::copyActionActivated);
    connect(m_pasteAction, &QAction::triggered, this, &PixmapEditor::pasteActionActivated);
#endif
    setSizePolicy(QSizePolicy(QSizePolicy::Preferred, QSizePolicy::Ignored));
    setFocusProxy(m_button);

#if QT_CONFIG(clipboard)
    connect(QApplication::clipboard(), &QClipboard::dataChanged,
            this, &PixmapEditor::clipboardDataChanged);
    clipboardDataChanged();
#endif
}

void PixmapEditor::setPixmapCache(DesignerPixmapCache *cache)
{
    m_pixmapCache = cache;
}

void PixmapEditor::setIconThemeModeEnabled(bool enabled)
{
    if (m_iconThemeModeEnabled == enabled)
        return;
    m_iconThemeModeEnabled = enabled;
    m_themeAction->setVisible(enabled);
}

void PixmapEditor::setSpacing(int spacing)
{
    m_layout->setSpacing(spacing);
}

void PixmapEditor::setPath(const QString &path)
{
    m_path = path;
    updateLabels();
}

void PixmapEditor::setTheme(const QString &theme)
{
    m_theme = theme;
    updateLabels();
}

void PixmapEditor::updateLabels()
{
    if (m_iconThemeModeEnabled && QIcon::hasThemeIcon(m_theme)) {
        m_pixmapLabel->setPixmap(QIcon::fromTheme(m_theme).pixmap(16, 16));
        m_pathLabel->setText(tr("[Theme] %1").arg(m_theme));
        m_copyAction->setEnabled(true);
    } else {
        if (m_path.isEmpty()) {
            m_pathLabel->setText(m_path);
            m_pixmapLabel->setPixmap(m_defaultPixmap);
            m_copyAction->setEnabled(false);
        } else {
            m_pathLabel->setText(QFileInfo(m_path).fileName());
            if (m_pixmapCache)
                m_pixmapLabel->setPixmap(QIcon(m_pixmapCache->pixmap(PropertySheetPixmapValue(m_path))).pixmap(16, 16));
            m_copyAction->setEnabled(true);
        }
    }
}

void PixmapEditor::setDefaultPixmap(const QPixmap &pixmap)
{
    m_defaultPixmap = QIcon(pixmap).pixmap(16, 16);
    const bool hasThemeIcon = m_iconThemeModeEnabled && QIcon::hasThemeIcon(m_theme);
    if (!hasThemeIcon && m_path.isEmpty())
        m_pixmapLabel->setPixmap(m_defaultPixmap);
}

void PixmapEditor::contextMenuEvent(QContextMenuEvent *event)
{
    QMenu menu(this);
    menu.addAction(m_copyAction);
    menu.addAction(m_pasteAction);
    menu.exec(event->globalPos());
    event->accept();
}

void PixmapEditor::defaultActionActivated()
{
    if (m_iconThemeModeEnabled && QIcon::hasThemeIcon(m_theme)) {
        themeActionActivated();
        return;
    }
    // Default to resource
    const PropertySheetPixmapValue::PixmapSource ps = m_path.isEmpty() ? PropertySheetPixmapValue::ResourcePixmap : PropertySheetPixmapValue::getPixmapSource(m_core, m_path);
    switch (ps) {
    case PropertySheetPixmapValue::LanguageResourcePixmap:
    case PropertySheetPixmapValue::ResourcePixmap:
        resourceActionActivated();
        break;
    case PropertySheetPixmapValue::FilePixmap:
        fileActionActivated();
        break;
    }
}

void PixmapEditor::resourceActionActivated()
{
    const QString oldPath = m_path;
    const  QString newPath = IconSelector::choosePixmapResource(m_core, m_core->resourceModel(), oldPath, this);
    if (!newPath.isEmpty() &&  newPath != oldPath) {
        setTheme(QString());
        setPath(newPath);
        emit pathChanged(newPath);
    }
}

void PixmapEditor::fileActionActivated()
{
    const QString newPath = IconSelector::choosePixmapFile(m_path, m_core->dialogGui(), this);
    if (!newPath.isEmpty() && newPath != m_path) {
        setTheme(QString());
        setPath(newPath);
        emit pathChanged(newPath);
    }
}

void PixmapEditor::themeActionActivated()
{
    bool ok;
    const QString newTheme = IconThemeDialog::getTheme(this, m_theme, &ok);
    if (ok && newTheme != m_theme) {
        setTheme(newTheme);
        setPath(QString());
        emit themeChanged(newTheme);
    }
}

#if QT_CONFIG(clipboard)
void PixmapEditor::copyActionActivated()
{
    QClipboard *clipboard = QApplication::clipboard();
    if (m_iconThemeModeEnabled && QIcon::hasThemeIcon(m_theme))
        clipboard->setText(m_theme);
    else
        clipboard->setText(m_path);
}

void PixmapEditor::pasteActionActivated()
{
    QClipboard *clipboard = QApplication::clipboard();
    QString subtype = QStringLiteral("plain");
    QString text = clipboard->text(subtype);
    if (!text.isNull()) {
        QStringList list = text.split(QLatin1Char('\n'));
        if (!list.isEmpty()) {
            text = list.at(0);
            if (m_iconThemeModeEnabled && QIcon::hasThemeIcon(text)) {
                setTheme(text);
                setPath(QString());
                emit themeChanged(text);
            } else {
                setPath(text);
                setTheme(QString());
                emit pathChanged(text);
            }
        }
    }
}

void PixmapEditor::clipboardDataChanged()
{
    QClipboard *clipboard = QApplication::clipboard();
    QString subtype = QStringLiteral("plain");
    const QString text = clipboard->text(subtype);
    m_pasteAction->setEnabled(!text.isNull());
}
#endif

// --------------- ResetWidget
class ResetWidget : public QWidget
{
    Q_OBJECT
public:
    ResetWidget(QtProperty *property, QWidget *parent = nullptr);

    void setWidget(QWidget *widget);
    void setResetEnabled(bool enabled);
    void setValueText(const QString &text);
    void setValueIcon(const QIcon &icon);
    void setSpacing(int spacing);
signals:
    void resetProperty(QtProperty *property);
private slots:
    void slotClicked();
private:
    QtProperty *m_property;
    QLabel *m_textLabel;
    QLabel *m_iconLabel;
    QToolButton *m_button;
    int m_spacing;
};

ResetWidget::ResetWidget(QtProperty *property, QWidget *parent) :
    QWidget(parent),
    m_property(property),
    m_textLabel(new QLabel(this)),
    m_iconLabel(new QLabel(this)),
    m_button(new QToolButton(this)),
    m_spacing(-1)
{
    m_textLabel->setSizePolicy(QSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed));
    m_iconLabel->setSizePolicy(QSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed));
    m_button->setToolButtonStyle(Qt::ToolButtonIconOnly);
    m_button->setIcon(createIconSet(QStringLiteral("resetproperty.png")));
    m_button->setIconSize(QSize(8,8));
    m_button->setSizePolicy(QSizePolicy(QSizePolicy::Fixed, QSizePolicy::MinimumExpanding));
    connect(m_button, &QAbstractButton::clicked, this, &ResetWidget::slotClicked);
    QLayout *layout = new QHBoxLayout(this);
    layout->setContentsMargins(QMargins());
    layout->setSpacing(m_spacing);
    layout->addWidget(m_iconLabel);
    layout->addWidget(m_textLabel);
    layout->addWidget(m_button);
    setFocusProxy(m_textLabel);
    setSizePolicy(QSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed));
}

void ResetWidget::setSpacing(int spacing)
{
    m_spacing = spacing;
    layout()->setSpacing(m_spacing);
}

void ResetWidget::setWidget(QWidget *widget)
{
    if (m_textLabel) {
        delete m_textLabel;
        m_textLabel = nullptr;
    }
    if (m_iconLabel) {
        delete m_iconLabel;
        m_iconLabel = nullptr;
    }
    delete layout();
    QLayout *layout = new QHBoxLayout(this);
    layout->setContentsMargins(QMargins());
    layout->setSpacing(m_spacing);
    layout->addWidget(widget);
    layout->addWidget(m_button);
    setFocusProxy(widget);
}

void ResetWidget::setResetEnabled(bool enabled)
{
    m_button->setEnabled(enabled);
}

void ResetWidget::setValueText(const QString &text)
{
    if (m_textLabel)
        m_textLabel->setText(text);
}

void ResetWidget::setValueIcon(const QIcon &icon)
{
    QPixmap pix = icon.pixmap(QSize(16, 16));
    if (m_iconLabel) {
        m_iconLabel->setVisible(!pix.isNull());
        m_iconLabel->setPixmap(pix);
    }
}

void ResetWidget::slotClicked()
{
    emit resetProperty(m_property);
}


// ------------ DesignerPropertyManager:

DesignerPropertyManager::DesignerPropertyManager(QDesignerFormEditorInterface *core, QObject *parent) :
    QtVariantPropertyManager(parent),
    m_changingSubValue(false),
    m_core(core),
    m_object(nullptr),
    m_sourceOfChange(nullptr)
{
    connect(this, &QtVariantPropertyManager::valueChanged,
            this, &DesignerPropertyManager::slotValueChanged);
    connect(this, & QtAbstractPropertyManager::propertyDestroyed,
            this, &DesignerPropertyManager::slotPropertyDestroyed);
}

DesignerPropertyManager::~DesignerPropertyManager()
{
    clear();
}

bool DesignerPropertyManager::m_IdBasedTranslations = false;

int DesignerPropertyManager::bitCount(int mask) const
{
    int count = 0;
    for (; mask; count++)
        mask &= mask - 1; // clear the least significant bit set
    return count;
}

int DesignerPropertyManager::alignToIndexH(uint align) const
{
    if (align & Qt::AlignLeft)
        return 0;
    if (align & Qt::AlignHCenter)
        return 1;
    if (align & Qt::AlignRight)
        return 2;
    if (align & Qt::AlignJustify)
        return 3;
    return 0;
}

int DesignerPropertyManager::alignToIndexV(uint align) const
{
    if (align & Qt::AlignTop)
        return 0;
    if (align & Qt::AlignVCenter)
        return 1;
    if (align & Qt::AlignBottom)
        return 2;
    return 1;
}

uint DesignerPropertyManager::indexHToAlign(int idx) const
{
    switch (idx) {
        case 0: return Qt::AlignLeft;
        case 1: return Qt::AlignHCenter;
        case 2: return Qt::AlignRight;
        case 3: return Qt::AlignJustify;
        default: break;
    }
    return Qt::AlignLeft;
}

uint DesignerPropertyManager::indexVToAlign(int idx) const
{
    switch (idx) {
        case 0: return Qt::AlignTop;
        case 1: return Qt::AlignVCenter;
        case 2: return Qt::AlignBottom;
        default: break;
    }
    return Qt::AlignVCenter;
}

QString DesignerPropertyManager::indexHToString(int idx) const
{
    switch (idx) {
        case 0: return tr("AlignLeft");
        case 1: return tr("AlignHCenter");
        case 2: return tr("AlignRight");
        case 3: return tr("AlignJustify");
        default: break;
    }
    return tr("AlignLeft");
}

QString DesignerPropertyManager::indexVToString(int idx) const
{
    switch (idx) {
        case 0: return tr("AlignTop");
        case 1: return tr("AlignVCenter");
        case 2: return tr("AlignBottom");
        default: break;
    }
    return tr("AlignVCenter");
}

void DesignerPropertyManager::slotValueChanged(QtProperty *property, const QVariant &value)
{
    if (m_changingSubValue)
        return;
    bool enableSubPropertyHandling = true;

    // Find a matching manager
    int subResult = m_stringManager.valueChanged(this, property, value);
    if (subResult == NoMatch)
        subResult = m_keySequenceManager.valueChanged(this, property, value);
    if (subResult == NoMatch)
        subResult = m_stringListManager.valueChanged(this, property, value);
    if (subResult == NoMatch)
        subResult = m_brushManager.valueChanged(this, property, value);
    if (subResult == NoMatch)
        subResult = m_fontManager.valueChanged(this, property, value);
    if (subResult != NoMatch) {
        if (subResult == Changed)
            emit valueChanged(property, value, enableSubPropertyHandling);
        return;
    }

    if (QtProperty *flagProperty = m_flagToProperty.value(property, 0)) {
        const auto subFlags = m_propertyToFlags.value(flagProperty);
        const qsizetype subFlagCount = subFlags.size();
        // flag changed
        const bool subValue = variantProperty(property)->value().toBool();
        const qsizetype subIndex = subFlags.indexOf(property);
        if (subIndex < 0)
            return;

        uint newValue = 0;

        m_changingSubValue = true;

        FlagData data = m_flagValues.value(flagProperty);
        const auto values = data.values;
        // Compute new value, without including (additional) supermasks
        if (values.at(subIndex) == 0) {
            for (qsizetype i = 0; i < subFlagCount; ++i) {
                QtVariantProperty *subFlag = variantProperty(subFlags.at(i));
                subFlag->setValue(i == subIndex);
            }
        } else {
            if (subValue)
                newValue = values.at(subIndex); // value mask of subValue
            for (qsizetype i = 0; i < subFlagCount; ++i) {
                QtVariantProperty *subFlag = variantProperty(subFlags.at(i));
                if (subFlag->value().toBool() && bitCount(values.at(i)) == 1)
                    newValue |= values.at(i);
            }
            if (newValue == 0) {
                // Uncheck all items except 0-mask
                for (qsizetype i = 0; i < subFlagCount; ++i) {
                    QtVariantProperty *subFlag = variantProperty(subFlags.at(i));
                    subFlag->setValue(values.at(i) == 0);
                }
            } else if (newValue == data.val) {
                if (!subValue && bitCount(values.at(subIndex)) > 1) {
                    // We unchecked something, but the original value still holds
                    variantProperty(property)->setValue(true);
                }
            } else {
                // Make sure 0-mask is not selected
                for (qsizetype i = 0; i < subFlagCount; ++i) {
                    QtVariantProperty *subFlag = variantProperty(subFlags.at(i));
                    if (values.at(i) == 0)
                        subFlag->setValue(false);
                }
                // Check/uncheck proper masks
                if (subValue) {
                    // Make sure submasks and supermasks are selected
                    for (qsizetype i = 0; i < subFlagCount; ++i) {
                        QtVariantProperty *subFlag = variantProperty(subFlags.at(i));
                        const uint vi = values.at(i);
                        if ((vi != 0) && ((vi & newValue) == vi) && !subFlag->value().toBool())
                            subFlag->setValue(true);
                    }
                } else {
                    // Make sure supermasks are not selected if they're no longer valid
                    for (qsizetype i = 0; i < subFlagCount; ++i) {
                        QtVariantProperty *subFlag = variantProperty(subFlags.at(i));
                        const uint vi = values.at(i);
                        if (subFlag->value().toBool() && ((vi & newValue) != vi))
                            subFlag->setValue(false);
                    }
                }
            }
        }
        m_changingSubValue = false;
        data.val = newValue;
        QVariant v;
        v.setValue(data.val);
        variantProperty(flagProperty)->setValue(v);
    } else if (QtProperty *alignProperty = m_alignHToProperty.value(property, 0)) {
        const uint v = m_alignValues.value(alignProperty);
        const uint newValue = indexHToAlign(value.toInt()) | indexVToAlign(alignToIndexV(v));
        if (v == newValue)
            return;

        variantProperty(alignProperty)->setValue(newValue);
    } else if (QtProperty *alignProperty = m_alignVToProperty.value(property, 0)) {
        const uint v = m_alignValues.value(alignProperty);
        const uint newValue = indexVToAlign(value.toInt()) | indexHToAlign(alignToIndexH(v));
        if (v == newValue)
            return;

        variantProperty(alignProperty)->setValue(newValue);
    } else if (QtProperty *iProperty = m_iconSubPropertyToProperty.value(property, 0)) {
        QtVariantProperty *iconProperty = variantProperty(iProperty);
        PropertySheetIconValue icon = qvariant_cast<PropertySheetIconValue>(iconProperty->value());
        QMap<QtProperty *, QPair<QIcon::Mode, QIcon::State> >::ConstIterator itState = m_iconSubPropertyToState.constFind(property);
        if (itState != m_iconSubPropertyToState.constEnd()) {
            QPair<QIcon::Mode, QIcon::State> pair = m_iconSubPropertyToState.value(property);
            icon.setPixmap(pair.first, pair.second, qvariant_cast<PropertySheetPixmapValue>(value));
        } else { // must be theme property
            icon.setTheme(value.toString());
        }
        QtProperty *origSourceOfChange = m_sourceOfChange;
        if (!origSourceOfChange)
            m_sourceOfChange = property;
        iconProperty->setValue(QVariant::fromValue(icon));
        if (!origSourceOfChange)
            m_sourceOfChange = origSourceOfChange;
    } else if (m_iconValues.contains(property)) {
        enableSubPropertyHandling = m_sourceOfChange;
    }
    emit valueChanged(property, value, enableSubPropertyHandling);
}

void DesignerPropertyManager::slotPropertyDestroyed(QtProperty *property)
{
    if (QtProperty *flagProperty = m_flagToProperty.value(property, 0)) {
        PropertyToPropertyListMap::iterator it = m_propertyToFlags.find(flagProperty);
        auto &propertyList = it.value();
        propertyList.replace(propertyList.indexOf(property), 0);
        m_flagToProperty.remove(property);
    } else if (QtProperty *alignProperty = m_alignHToProperty.value(property, 0)) {
        m_propertyToAlignH.remove(alignProperty);
        m_alignHToProperty.remove(property);
    } else if (QtProperty *alignProperty = m_alignVToProperty.value(property, 0)) {
        m_propertyToAlignV.remove(alignProperty);
        m_alignVToProperty.remove(property);
    } else if (m_stringManager.destroy(property)
               || m_stringListManager.destroy(property)
               || m_keySequenceManager.destroy(property)) {
    } else if (QtProperty *iconProperty = m_iconSubPropertyToProperty.value(property, 0)) {
        if (m_propertyToTheme.value(iconProperty) == property) {
            m_propertyToTheme.remove(iconProperty);
        } else {
            QMap<QtProperty *, QMap<QPair<QIcon::Mode, QIcon::State>, QtProperty *> >::iterator it =
                        m_propertyToIconSubProperties.find(iconProperty);
            QPair<QIcon::Mode, QIcon::State> state = m_iconSubPropertyToState.value(property);
            QMap<QPair<QIcon::Mode, QIcon::State>, QtProperty *> &propertyList = it.value();
            propertyList.remove(state);
            m_iconSubPropertyToState.remove(property);
        }
        m_iconSubPropertyToProperty.remove(property);
    } else {
        m_fontManager.slotPropertyDestroyed(property);
        m_brushManager.slotPropertyDestroyed(property);
    }
    m_alignDefault.remove(property);
}

QStringList DesignerPropertyManager::attributes(int propertyType) const
{
    if (!isPropertyTypeSupported(propertyType))
        return QStringList();

    QStringList list = QtVariantPropertyManager::attributes(propertyType);
    if (propertyType == designerFlagTypeId()) {
        list.append(QLatin1String(flagsAttributeC));
    } else if (propertyType == designerPixmapTypeId()) {
        list.append(QLatin1String(defaultResourceAttributeC));
    } else if (propertyType == designerIconTypeId()) {
        list.append(QLatin1String(defaultResourceAttributeC));
    } else if (propertyType == designerStringTypeId() || propertyType == QMetaType::QString) {
        list.append(QLatin1String(validationModesAttributeC));
        list.append(QLatin1String(fontAttributeC));
        list.append(QLatin1String(themeAttributeC));
    } else if (propertyType == QMetaType::QPalette) {
        list.append(QLatin1String(superPaletteAttributeC));
    }
    list.append(QLatin1String(resettableAttributeC));
    return list;
}

int DesignerPropertyManager::attributeType(int propertyType, const QString &attribute) const
{
    if (!isPropertyTypeSupported(propertyType))
        return 0;

    if (propertyType == designerFlagTypeId() && attribute == QLatin1String(flagsAttributeC))
        return designerFlagListTypeId();
    if (propertyType == designerPixmapTypeId() && attribute == QLatin1String(defaultResourceAttributeC))
        return QMetaType::QPixmap;
    if (propertyType == designerIconTypeId() && attribute == QLatin1String(defaultResourceAttributeC))
        return QMetaType::QIcon;
    if (attribute == QLatin1String(resettableAttributeC))
        return QMetaType::Bool;
    if (propertyType == designerStringTypeId() || propertyType == QMetaType::QString) {
        if (attribute == QLatin1String(validationModesAttributeC))
            return QMetaType::Int;
        if (attribute == QLatin1String(fontAttributeC))
            return QMetaType::QFont;
        if (attribute == QLatin1String(themeAttributeC))
            return QMetaType::Bool;
    }
    if (propertyType == QMetaType::QPalette && attribute == QLatin1String(superPaletteAttributeC))
        return QMetaType::QPalette;

    return QtVariantPropertyManager::attributeType(propertyType, attribute);
}

QVariant DesignerPropertyManager::attributeValue(const QtProperty *property, const QString &attribute) const
{
    QtProperty *prop = const_cast<QtProperty *>(property);

    if (attribute == QLatin1String(resettableAttributeC)) {
        const PropertyBoolMap::const_iterator it = m_resetMap.constFind(prop);
        if (it != m_resetMap.constEnd())
            return it.value();
    }

    if (attribute == QLatin1String(flagsAttributeC)) {
        PropertyFlagDataMap::const_iterator it = m_flagValues.constFind(prop);
        if (it != m_flagValues.constEnd()) {
            QVariant v;
            v.setValue(it.value().flags);
            return v;
        }
    }
    if (attribute == QLatin1String(validationModesAttributeC)) {
        const PropertyIntMap::const_iterator it = m_stringAttributes.constFind(prop);
        if (it !=  m_stringAttributes.constEnd())
            return it.value();
    }

    if (attribute == QLatin1String(fontAttributeC)) {
        const PropertyFontMap::const_iterator it = m_stringFontAttributes.constFind(prop);
        if (it !=  m_stringFontAttributes.constEnd())
            return it.value();
    }

    if (attribute == QLatin1String(themeAttributeC)) {
        const PropertyBoolMap::const_iterator it = m_stringThemeAttributes.constFind(prop);
        if (it !=  m_stringThemeAttributes.constEnd())
            return it.value();
    }

    if (attribute == QLatin1String(superPaletteAttributeC)) {
        PropertyPaletteDataMap::const_iterator it = m_paletteValues.constFind(prop);
        if (it !=  m_paletteValues.constEnd())
            return it.value().superPalette;
    }

    if (attribute == QLatin1String(defaultResourceAttributeC)) {
        QMap<QtProperty *, QPixmap>::const_iterator itPix = m_defaultPixmaps.constFind(prop);
        if (itPix != m_defaultPixmaps.constEnd())
            return itPix.value();

        QMap<QtProperty *, QIcon>::const_iterator itIcon = m_defaultIcons.constFind(prop);
        if (itIcon != m_defaultIcons.constEnd())
            return itIcon.value();
    }

    if (attribute == alignDefaultAttribute()) {
        Qt::Alignment v = m_alignDefault.value(property,
                                               Qt::Alignment(Qt::AlignLeading | Qt::AlignHCenter));
        return QVariant(uint(v));
    }

    return QtVariantPropertyManager::attributeValue(property, attribute);
}

void DesignerPropertyManager::setAttribute(QtProperty *property,
            const QString &attribute, const QVariant &value)
{
    if (attribute == QLatin1String(resettableAttributeC) && m_resetMap.contains(property)) {
        if (value.userType() != QMetaType::Bool)
            return;
        const bool val = value.toBool();
        const PropertyBoolMap::iterator it = m_resetMap.find(property);
        if (it.value() == val)
            return;
        it.value() = val;
        emit attributeChanged(variantProperty(property), attribute, value);
        return;
    }
    if (attribute == QLatin1String(flagsAttributeC) && m_flagValues.contains(property)) {
        if (value.userType() != designerFlagListTypeId())
            return;

        const DesignerFlagList flags = qvariant_cast<DesignerFlagList>(value);
        PropertyFlagDataMap::iterator fit = m_flagValues.find(property);
        FlagData data = fit.value();
        if (data.flags == flags)
            return;

        PropertyToPropertyListMap::iterator pfit = m_propertyToFlags.find(property);
        for (QtProperty *prop : std::as_const(pfit.value())) {
            if (prop) {
                delete prop;
                m_flagToProperty.remove(prop);
            }
        }
        pfit.value().clear();

        QList<uint> values;

        for (const QPair<QString, uint> &pair : flags) {
            const QString flagName = pair.first;
            QtProperty *prop = addProperty(QMetaType::Bool);
            prop->setPropertyName(flagName);
            property->addSubProperty(prop);
            m_propertyToFlags[property].append(prop);
            m_flagToProperty[prop] = property;
            values.append(pair.second);
        }

        data.val = 0;
        data.flags = flags;
        data.values = values;

        fit.value() = data;

        QVariant v;
        v.setValue(flags);
        emit attributeChanged(property, attribute, v);

        emit propertyChanged(property);
        emit QtVariantPropertyManager::valueChanged(property, data.val);
    } else if (attribute == QLatin1String(validationModesAttributeC) && m_stringAttributes.contains(property)) {
        if (value.userType() != QMetaType::Int)
            return;

        const PropertyIntMap::iterator it = m_stringAttributes.find(property);
        const int oldValue = it.value();

        const int newValue = value.toInt();

        if (oldValue == newValue)
            return;

        it.value() = newValue;

        emit attributeChanged(property, attribute, newValue);
    } else if (attribute == QLatin1String(fontAttributeC) && m_stringFontAttributes.contains(property)) {
        if (value.userType() != QMetaType::QFont)
            return;

        const PropertyFontMap::iterator it = m_stringFontAttributes.find(property);
        const QFont oldValue = it.value();

        const QFont newValue = qvariant_cast<QFont>(value);

        if (oldValue == newValue)
            return;

        it.value() = newValue;

        emit attributeChanged(property, attribute, newValue);
    } else if (attribute == QLatin1String(themeAttributeC) && m_stringThemeAttributes.contains(property)) {
        if (value.userType() != QMetaType::Bool)
            return;

        const PropertyBoolMap::iterator it = m_stringThemeAttributes.find(property);
        const bool oldValue = it.value();

        const bool newValue = value.toBool();

        if (oldValue == newValue)
            return;

        it.value() = newValue;

        emit attributeChanged(property, attribute, newValue);
    } else if (attribute == QLatin1String(superPaletteAttributeC) && m_paletteValues.contains(property)) {
        if (value.userType() != QMetaType::QPalette)
            return;

        QPalette superPalette = qvariant_cast<QPalette>(value);

        const PropertyPaletteDataMap::iterator it = m_paletteValues.find(property);
        PaletteData data = it.value();
        if (data.superPalette == superPalette)
            return;

        data.superPalette = superPalette;
        // resolve here
        const uint mask = data.val.resolveMask();
        data.val = data.val.resolve(superPalette);
        data.val.setResolveMask(mask);

        it.value() = data;

        QVariant v;
        v.setValue(superPalette);
        emit attributeChanged(property, attribute, v);

        emit propertyChanged(property);
        emit QtVariantPropertyManager::valueChanged(property, data.val); // if resolve was done, this is also for consistency
    } else if (attribute == QLatin1String(defaultResourceAttributeC) && m_defaultPixmaps.contains(property)) {
        if (value.userType() != QMetaType::QPixmap)
            return;

        QPixmap defaultPixmap = qvariant_cast<QPixmap>(value);

        const QMap<QtProperty *, QPixmap>::iterator it = m_defaultPixmaps.find(property);
        QPixmap oldDefaultPixmap = it.value();
        if (defaultPixmap.cacheKey() == oldDefaultPixmap.cacheKey())
            return;

        it.value() = defaultPixmap;

        QVariant v = QVariant::fromValue(defaultPixmap);
        emit attributeChanged(property, attribute, v);

        emit propertyChanged(property);
    } else if (attribute == QLatin1String(defaultResourceAttributeC) && m_defaultIcons.contains(property)) {
        if (value.userType() != QMetaType::QIcon)
            return;

        QIcon defaultIcon = qvariant_cast<QIcon>(value);

        const QMap<QtProperty *, QIcon>::iterator it = m_defaultIcons.find(property);
        QIcon oldDefaultIcon = it.value();
        if (defaultIcon.cacheKey() == oldDefaultIcon.cacheKey())
            return;

        it.value() = defaultIcon;

        qdesigner_internal::PropertySheetIconValue icon = m_iconValues.value(property);
        if (icon.paths().isEmpty()) {
            QMap<QPair<QIcon::Mode, QIcon::State>, QtProperty *> subIconProperties = m_propertyToIconSubProperties.value(property);
            for (auto itSub = subIconProperties.cbegin(), end = subIconProperties.cend(); itSub != end; ++itSub) {
                QPair<QIcon::Mode, QIcon::State> pair = itSub.key();
                QtProperty *subProp = itSub.value();
                setAttribute(subProp, QLatin1String(defaultResourceAttributeC),
                             defaultIcon.pixmap(16, 16, pair.first, pair.second));
            }
        }

        QVariant v = QVariant::fromValue(defaultIcon);
        emit attributeChanged(property, attribute, v);

        emit propertyChanged(property);
    } else if (attribute == alignDefaultAttribute()) {
        m_alignDefault[property] = Qt::Alignment(value.toUInt());
    }
    QtVariantPropertyManager::setAttribute(property, attribute, value);
}

int DesignerPropertyManager::designerFlagTypeId()
{
    static const int rc = qMetaTypeId<DesignerFlagPropertyType>();
    return rc;
}

int DesignerPropertyManager::designerFlagListTypeId()
{
    static const int rc = qMetaTypeId<DesignerFlagList>();
    return rc;
}

int DesignerPropertyManager::designerAlignmentTypeId()
{
    static const int rc = qMetaTypeId<DesignerAlignmentPropertyType>();
    return rc;
}

int DesignerPropertyManager::designerPixmapTypeId()
{
    return qMetaTypeId<PropertySheetPixmapValue>();
}

int DesignerPropertyManager::designerIconTypeId()
{
    return qMetaTypeId<PropertySheetIconValue>();
}

int DesignerPropertyManager::designerStringTypeId()
{
    return qMetaTypeId<PropertySheetStringValue>();
}

int DesignerPropertyManager::designerStringListTypeId()
{
    return qMetaTypeId<PropertySheetStringListValue>();
}

int DesignerPropertyManager::designerKeySequenceTypeId()
{
    return qMetaTypeId<PropertySheetKeySequenceValue>();
}

QString DesignerPropertyManager::alignDefaultAttribute()
{
    return QStringLiteral("alignDefault");
}

uint DesignerPropertyManager::alignDefault(const QtVariantProperty *prop)
{
    return prop->attributeValue(DesignerPropertyManager::alignDefaultAttribute()).toUInt();
}

bool DesignerPropertyManager::isPropertyTypeSupported(int propertyType) const
{
    switch (propertyType) {
    case QMetaType::QPalette:
    case QMetaType::UInt:
    case QMetaType::LongLong:
    case QMetaType::ULongLong:
    case QMetaType::QUrl:
    case QMetaType::QByteArray:
    case QMetaType::QStringList:
    case QMetaType::QBrush:
        return true;
    default:
        break;
    }

    if (propertyType == designerFlagTypeId())
        return true;
    if (propertyType == designerAlignmentTypeId())
        return true;
    if (propertyType == designerPixmapTypeId())
        return true;
    if (propertyType == designerIconTypeId())
        return true;
    if (propertyType == designerStringTypeId() || propertyType == designerStringListTypeId())
        return true;
    if (propertyType == designerKeySequenceTypeId())
        return true;

    return QtVariantPropertyManager::isPropertyTypeSupported(propertyType);
}

QString DesignerPropertyManager::valueText(const QtProperty *property) const
{
    if (m_flagValues.contains(const_cast<QtProperty *>(property))) {
        const FlagData data = m_flagValues.value(const_cast<QtProperty *>(property));
        const uint v = data.val;
        const QChar bar = QLatin1Char('|');
        QString valueStr;
        for (const DesignerIntPair &p : data.flags) {
            const uint val = p.second;
            const bool checked = (val == 0) ? (v == 0) : ((val & v) == val);
            if (checked) {
                if (!valueStr.isEmpty())
                    valueStr += bar;
                valueStr += p.first;
            }
        }
        return valueStr;
    }
    if (m_alignValues.contains(const_cast<QtProperty *>(property))) {
        const uint v = m_alignValues.value(const_cast<QtProperty *>(property));
        return tr("%1, %2").arg(indexHToString(alignToIndexH(v)),
                                indexVToString(alignToIndexV(v)));
    }
    if (m_paletteValues.contains(const_cast<QtProperty *>(property))) {
        const PaletteData data = m_paletteValues.value(const_cast<QtProperty *>(property));
        const uint mask = data.val.resolveMask();
        if (mask)
            return tr("Customized (%n roles)", nullptr, bitCount(mask));
        static const QString inherited = tr("Inherited");
        return inherited;
    }
    if (m_iconValues.contains(const_cast<QtProperty *>(property))) {
        const PropertySheetIconValue icon = m_iconValues.value(const_cast<QtProperty *>(property));
        const QString theme = icon.theme();
        if (!theme.isEmpty() && QIcon::hasThemeIcon(theme))
            return tr("[Theme] %1").arg(theme);
        const auto &paths = icon.paths();
        const PropertySheetIconValue::ModeStateToPixmapMap::const_iterator it = paths.constFind(qMakePair(QIcon::Normal, QIcon::Off));
        if (it == paths.constEnd())
            return QString();
        return QFileInfo(it.value().path()).fileName();
    }
    if (m_pixmapValues.contains(const_cast<QtProperty *>(property))) {
        const QString path =  m_pixmapValues.value(const_cast<QtProperty *>(property)).path();
        if (path.isEmpty())
            return QString();
        return QFileInfo(path).fileName();
    }
    if (m_uintValues.contains(const_cast<QtProperty *>(property))) {
        return QString::number(m_uintValues.value(const_cast<QtProperty *>(property)));
    }
    if (m_longLongValues.contains(const_cast<QtProperty *>(property))) {
        return QString::number(m_longLongValues.value(const_cast<QtProperty *>(property)));
    }
    if (m_uLongLongValues.contains(const_cast<QtProperty *>(property))) {
        return QString::number(m_uLongLongValues.value(const_cast<QtProperty *>(property)));
    }
    if (m_urlValues.contains(const_cast<QtProperty *>(property))) {
        return m_urlValues.value(const_cast<QtProperty *>(property)).toString();
    }
    if (m_byteArrayValues.contains(const_cast<QtProperty *>(property))) {
        return QString::fromUtf8(m_byteArrayValues.value(const_cast<QtProperty *>(property)));
    }
    const int vType = QtVariantPropertyManager::valueType(property);
    if (vType == QMetaType::QString || vType == designerStringTypeId()) {
        const QString str = (QtVariantPropertyManager::valueType(property) == QMetaType::QString)
            ? value(property).toString() : qvariant_cast<PropertySheetStringValue>(value(property)).value();
        const int validationMode = attributeValue(property, QLatin1String(validationModesAttributeC)).toInt();
        return TextPropertyEditor::stringToEditorString(str, static_cast<TextPropertyValidationMode>(validationMode));
    }
    if (vType == QMetaType::QStringList || vType == designerStringListTypeId()) {
        QVariant v = value(property);
        const QStringList list = v.metaType().id() == QMetaType::QStringList
            ? v.toStringList() : qvariant_cast<PropertySheetStringListValue>(v).value();
        return list.join(QLatin1String("; "));
    }
    if (vType == designerKeySequenceTypeId()) {
        return qvariant_cast<PropertySheetKeySequenceValue>(value(property)).value().toString(QKeySequence::NativeText);
    }
    if (vType == QMetaType::Bool) {
        return QString();
    }

    QString rc;
    if (m_brushManager.valueText(property, &rc))
        return rc;
    return QtVariantPropertyManager::valueText(property);
}

void DesignerPropertyManager::reloadResourceProperties()
{
    DesignerIconCache *iconCache = nullptr;
    for (auto itIcon = m_iconValues.cbegin(), end = m_iconValues.cend(); itIcon!= end; ++itIcon) {
        QtProperty *property = itIcon.key();
        const PropertySheetIconValue &icon = itIcon.value();

        QIcon defaultIcon = m_defaultIcons.value(property);
        if (!icon.paths().isEmpty()) {
            if (!iconCache) {
                QDesignerFormWindowInterface *formWindow = QDesignerFormWindowInterface::findFormWindow(m_object);
                qdesigner_internal::FormWindowBase *fwb = qobject_cast<qdesigner_internal::FormWindowBase *>(formWindow);
                iconCache = fwb->iconCache();
            }
            if (iconCache)
                defaultIcon = iconCache->icon(icon);
        }

        QMap<QPair<QIcon::Mode, QIcon::State>, QtProperty *> subProperties = m_propertyToIconSubProperties.value(property);
        for (auto itSub = subProperties.cbegin(), end = subProperties.cend(); itSub != end; ++itSub) {
            const QPair<QIcon::Mode, QIcon::State> pair = itSub.key();
            QtVariantProperty *subProperty = variantProperty(itSub.value());
            subProperty->setAttribute(QLatin1String(defaultResourceAttributeC),
                                      defaultIcon.pixmap(16, 16, pair.first, pair.second));
        }

        emit propertyChanged(property);
        emit QtVariantPropertyManager::valueChanged(property, QVariant::fromValue(itIcon.value()));
    }
    for (auto itPix = m_pixmapValues.cbegin(), end = m_pixmapValues.cend(); itPix != end; ++itPix) {
        QtProperty *property = itPix.key();
        emit propertyChanged(property);
        emit QtVariantPropertyManager::valueChanged(property, QVariant::fromValue(itPix.value()));
    }
}

QIcon DesignerPropertyManager::valueIcon(const QtProperty *property) const
{
    if (m_iconValues.contains(const_cast<QtProperty *>(property))) {
        if (!property->isModified())
            return m_defaultIcons.value(const_cast<QtProperty *>(property)).pixmap(16, 16);
        QDesignerFormWindowInterface *formWindow = QDesignerFormWindowInterface::findFormWindow(m_object);
        qdesigner_internal::FormWindowBase *fwb = qobject_cast<qdesigner_internal::FormWindowBase *>(formWindow);
        if (fwb)
            return fwb->iconCache()->icon(m_iconValues.value(const_cast<QtProperty *>(property))).pixmap(16, 16);
    } else if (m_pixmapValues.contains(const_cast<QtProperty *>(property))) {
        if (!property->isModified())
            return m_defaultPixmaps.value(const_cast<QtProperty *>(property));
        QDesignerFormWindowInterface *formWindow = QDesignerFormWindowInterface::findFormWindow(m_object);
        qdesigner_internal::FormWindowBase *fwb = qobject_cast<qdesigner_internal::FormWindowBase *>(formWindow);
        if (fwb)
            return fwb->pixmapCache()->pixmap(m_pixmapValues.value(const_cast<QtProperty *>(property)));
    } else if (m_stringThemeAttributes.value(const_cast<QtProperty *>(property), false)) {
        return QIcon::fromTheme(value(property).toString());
    } else {
        QIcon rc;
        if (m_brushManager.valueIcon(property, &rc))
            return rc;
    }

    return QtVariantPropertyManager::valueIcon(property);
}

QVariant DesignerPropertyManager::value(const QtProperty *property) const
{
    if (m_flagValues.contains(const_cast<QtProperty *>(property)))
        return m_flagValues.value(const_cast<QtProperty *>(property)).val;
    if (m_alignValues.contains(const_cast<QtProperty *>(property)))
        return m_alignValues.value(const_cast<QtProperty *>(property));
    if (m_paletteValues.contains(const_cast<QtProperty *>(property)))
        return m_paletteValues.value(const_cast<QtProperty *>(property)).val;
    if (m_iconValues.contains(const_cast<QtProperty *>(property)))
        return QVariant::fromValue(m_iconValues.value(const_cast<QtProperty *>(property)));
    if (m_pixmapValues.contains(const_cast<QtProperty *>(property)))
        return QVariant::fromValue(m_pixmapValues.value(const_cast<QtProperty *>(property)));
    QVariant rc;
    if (m_stringManager.value(property, &rc)
        || m_keySequenceManager.value(property, &rc)
        || m_stringListManager.value(property, &rc)
        || m_brushManager.value(property, &rc))
        return rc;
    if (m_uintValues.contains(const_cast<QtProperty *>(property)))
        return m_uintValues.value(const_cast<QtProperty *>(property));
    if (m_longLongValues.contains(const_cast<QtProperty *>(property)))
        return m_longLongValues.value(const_cast<QtProperty *>(property));
    if (m_uLongLongValues.contains(const_cast<QtProperty *>(property)))
        return m_uLongLongValues.value(const_cast<QtProperty *>(property));
    if (m_urlValues.contains(const_cast<QtProperty *>(property)))
        return m_urlValues.value(const_cast<QtProperty *>(property));
    if (m_byteArrayValues.contains(const_cast<QtProperty *>(property)))
        return m_byteArrayValues.value(const_cast<QtProperty *>(property));

    return QtVariantPropertyManager::value(property);
}

int DesignerPropertyManager::valueType(int propertyType) const
{
    switch (propertyType) {
    case QMetaType::QPalette:
    case QMetaType::UInt:
    case QMetaType::LongLong:
    case QMetaType::ULongLong:
    case QMetaType::QUrl:
    case QMetaType::QByteArray:
    case QMetaType::QStringList:
    case QMetaType::QBrush:
        return propertyType;
    default:
        break;
    }
    if (propertyType == designerFlagTypeId())
        return QMetaType::UInt;
    if (propertyType == designerAlignmentTypeId())
        return QMetaType::UInt;
    if (propertyType == designerPixmapTypeId())
        return propertyType;
    if (propertyType == designerIconTypeId())
        return propertyType;
    if (propertyType == designerStringTypeId() || propertyType == designerStringListTypeId())
        return propertyType;
    if (propertyType == designerKeySequenceTypeId())
        return propertyType;
    return QtVariantPropertyManager::valueType(propertyType);
}

void DesignerPropertyManager::setValue(QtProperty *property, const QVariant &value)
{
    int subResult = m_stringManager.setValue(this, property, designerStringTypeId(), value);
    if (subResult == NoMatch)
        subResult = m_stringListManager.setValue(this, property, designerStringListTypeId(), value);
    if (subResult == NoMatch)
        subResult = m_keySequenceManager.setValue(this, property, designerKeySequenceTypeId(), value);
    if (subResult == NoMatch)
        subResult = m_brushManager.setValue(this, property, value);
    if (subResult != NoMatch) {
        if (subResult == Changed) {
            emit QtVariantPropertyManager::valueChanged(property, value);
            emit propertyChanged(property);
        }
        return;
    }

    const PropertyFlagDataMap::iterator fit = m_flagValues.find(property);

    if (fit !=  m_flagValues.end()) {
        if (value.metaType().id() != QMetaType::UInt && !value.canConvert<uint>())
            return;

        const uint v = value.toUInt();

        FlagData data = fit.value();
        if (data.val == v)
            return;

        // set Value

        const auto values = data.values;
        const auto subFlags = m_propertyToFlags.value(property);
        const qsizetype subFlagCount = subFlags.size();
        for (qsizetype i = 0; i < subFlagCount; ++i) {
            QtVariantProperty *subFlag = variantProperty(subFlags.at(i));
            const uint val = values.at(i);
            const bool checked = (val == 0) ? (v == 0) : ((val & v) == val);
            subFlag->setValue(checked);
        }

        for (qsizetype i = 0; i < subFlagCount; ++i) {
            QtVariantProperty *subFlag = variantProperty(subFlags.at(i));
            const uint val = values.at(i);
            const bool checked = (val == 0) ? (v == 0) : ((val & v) == val);
            bool enabled = true;
            if (val == 0) {
                if (checked)
                    enabled = false;
            } else if (bitCount(val) > 1) {
                // Disabled if all flags contained in the mask are checked
                uint currentMask = 0;
                for (qsizetype j = 0; j < subFlagCount; ++j) {
                    QtVariantProperty *subFlag = variantProperty(subFlags.at(j));
                    if (bitCount(values.at(j)) == 1)
                        currentMask |= subFlag->value().toBool() ? values.at(j) : 0;
                }
                if ((currentMask & values.at(i)) == values.at(i))
                    enabled = false;
            }
            subFlag->setEnabled(enabled);
        }

        data.val = v;
        fit.value() = data;

        emit QtVariantPropertyManager::valueChanged(property, data.val);
        emit propertyChanged(property);

        return;
    }
    if (m_alignValues.contains(property)) {
        if (value.metaType().id() != QMetaType::UInt && !value.canConvert<uint>())
            return;

        const uint v = value.toUInt();

        uint val = m_alignValues.value(property);

        if (val == v)
            return;

        QtVariantProperty *alignH = variantProperty(m_propertyToAlignH.value(property));
        QtVariantProperty *alignV = variantProperty(m_propertyToAlignV.value(property));

        if (alignH)
            alignH->setValue(alignToIndexH(v));
        if (alignV)
            alignV->setValue(alignToIndexV(v));

        m_alignValues[property] = v;

        emit QtVariantPropertyManager::valueChanged(property, v);
        emit propertyChanged(property);

        return;
    }
    if (m_paletteValues.contains(property)) {
        if (value.metaType().id() != QMetaType::QPalette && !value.canConvert<QPalette>())
            return;

        QPalette p = qvariant_cast<QPalette>(value);

        PaletteData data = m_paletteValues.value(property);

        const uint mask = p.resolveMask();
        p = p.resolve(data.superPalette);
        p.setResolveMask(mask);

        if (data.val == p && data.val.resolveMask() == p.resolveMask())
            return;

        data.val = p;
        m_paletteValues[property] = data;

        emit QtVariantPropertyManager::valueChanged(property, data.val);
        emit propertyChanged(property);

        return;
    }
    if (m_iconValues.contains(property)) {
        if (value.userType() != designerIconTypeId())
            return;

        const PropertySheetIconValue icon = qvariant_cast<PropertySheetIconValue>(value);

        const PropertySheetIconValue oldIcon = m_iconValues.value(property);
        if (icon == oldIcon)
            return;

        m_iconValues[property] = icon;

        QIcon defaultIcon = m_defaultIcons.value(property);
        if (!icon.paths().isEmpty()) {
            QDesignerFormWindowInterface *formWindow = QDesignerFormWindowInterface::findFormWindow(m_object);
            qdesigner_internal::FormWindowBase *fwb = qobject_cast<qdesigner_internal::FormWindowBase *>(formWindow);
            if (fwb)
                defaultIcon = fwb->iconCache()->icon(icon);
        }

        const auto &iconPaths = icon.paths();

        QMap<QPair<QIcon::Mode, QIcon::State>, QtProperty *> subProperties = m_propertyToIconSubProperties.value(property);
        for (auto itSub = subProperties.cbegin(), end = subProperties.cend(); itSub != end; ++itSub) {
            const QPair<QIcon::Mode, QIcon::State> pair = itSub.key();
            QtVariantProperty *subProperty = variantProperty(itSub.value());
            bool hasPath = iconPaths.contains(pair);
            subProperty->setModified(hasPath);
            subProperty->setValue(QVariant::fromValue(iconPaths.value(pair)));
            subProperty->setAttribute(QLatin1String(defaultResourceAttributeC),
                                      defaultIcon.pixmap(16, 16, pair.first, pair.second));
        }
        QtVariantProperty *themeSubProperty = variantProperty(m_propertyToTheme.value(property));
        if (themeSubProperty) {
            const QString theme = icon.theme();
            themeSubProperty->setModified(!theme.isEmpty());
            themeSubProperty->setValue(theme);
        }

        emit QtVariantPropertyManager::valueChanged(property, QVariant::fromValue(icon));
        emit propertyChanged(property);

        QString toolTip;
        const QMap<QPair<QIcon::Mode, QIcon::State>, PropertySheetPixmapValue>::ConstIterator itNormalOff =
                    iconPaths.constFind(qMakePair(QIcon::Normal, QIcon::Off));
        if (itNormalOff != iconPaths.constEnd())
            toolTip = itNormalOff.value().path();
        // valueText() only show the file name; show full path as ToolTip.
        property->setToolTip(QDir::toNativeSeparators(toolTip));

        return;
    }
    if (m_pixmapValues.contains(property)) {
        if (value.userType() != designerPixmapTypeId())
            return;

        const PropertySheetPixmapValue pixmap = qvariant_cast<PropertySheetPixmapValue>(value);

        const PropertySheetPixmapValue oldPixmap = m_pixmapValues.value(property);
        if (pixmap == oldPixmap)
            return;

        m_pixmapValues[property] = pixmap;

        emit QtVariantPropertyManager::valueChanged(property, QVariant::fromValue(pixmap));
        emit propertyChanged(property);

        // valueText() only show the file name; show full path as ToolTip.
        property->setToolTip(QDir::toNativeSeparators(pixmap.path()));

        return;
    }
    if (m_uintValues.contains(property)) {
        if (value.metaType().id() != QMetaType::UInt && !value.canConvert<uint>())
            return;

        const uint v = value.toUInt(nullptr);

        const uint oldValue = m_uintValues.value(property);
        if (v == oldValue)
            return;

        m_uintValues[property] = v;

        emit QtVariantPropertyManager::valueChanged(property, v);
        emit propertyChanged(property);

        return;
    }
    if (m_longLongValues.contains(property)) {
        if (value.metaType().id() != QMetaType::LongLong && !value.canConvert<qlonglong>())
            return;

        const qlonglong v = value.toLongLong(nullptr);

        const qlonglong oldValue = m_longLongValues.value(property);
        if (v == oldValue)
            return;

        m_longLongValues[property] = v;

        emit QtVariantPropertyManager::valueChanged(property, v);
        emit propertyChanged(property);

        return;
    }
    if (m_uLongLongValues.contains(property)) {
        if (value.metaType().id() != QMetaType::ULongLong && !value.canConvert<qulonglong>())
            return;

        qulonglong v = value.toULongLong(nullptr);

        qulonglong oldValue = m_uLongLongValues.value(property);
        if (v == oldValue)
            return;

        m_uLongLongValues[property] = v;

        emit QtVariantPropertyManager::valueChanged(property, v);
        emit propertyChanged(property);

        return;
    }
    if (m_urlValues.contains(property)) {
        if (value.metaType().id() != QMetaType::QUrl && !value.canConvert<QUrl>())
            return;

        const QUrl v = value.toUrl();

        const QUrl oldValue = m_urlValues.value(property);
        if (v == oldValue)
            return;

        m_urlValues[property] = v;

        emit QtVariantPropertyManager::valueChanged(property, v);
        emit propertyChanged(property);

        return;
    }
    if (m_byteArrayValues.contains(property)) {
        if (value.metaType().id() != QMetaType::QByteArray && !value.canConvert<QByteArray>())
            return;

        const QByteArray v = value.toByteArray();

        const QByteArray oldValue = m_byteArrayValues.value(property);
        if (v == oldValue)
            return;

        m_byteArrayValues[property] = v;

        emit QtVariantPropertyManager::valueChanged(property, v);
        emit propertyChanged(property);

        return;
    }
    m_fontManager.setValue(this, property, value);
    QtVariantPropertyManager::setValue(property, value);
    if (QtVariantPropertyManager::valueType(property) == QMetaType::Bool)
        property->setToolTip(QtVariantPropertyManager::valueText(property));
}

void DesignerPropertyManager::initializeProperty(QtProperty *property)
{
    m_resetMap[property] = false;

    const int type = propertyType(property);
    m_fontManager.preInitializeProperty(property, type, m_resetMap);
    switch (type) {
    case QMetaType::QPalette:
        m_paletteValues[property] = PaletteData();
        break;
    case QMetaType::QString:
        m_stringAttributes[property] = ValidationSingleLine;
        m_stringFontAttributes[property] = QApplication::font();
        m_stringThemeAttributes[property] = false;
        break;
    case QMetaType::UInt:
        m_uintValues[property] = 0;
        break;
    case QMetaType::LongLong:
        m_longLongValues[property] = 0;
        break;
    case QMetaType::ULongLong:
        m_uLongLongValues[property] = 0;
        break;
    case QMetaType::QUrl:
        m_urlValues[property] = QUrl();
        break;
    case QMetaType::QByteArray:
        m_byteArrayValues[property] = QByteArray();
        break;
    case QMetaType::QBrush:
        m_brushManager.initializeProperty(this, property, enumTypeId());
        break;
    default:
        if (type == designerFlagTypeId()) {
            m_flagValues[property] = FlagData();
            m_propertyToFlags[property] = QList<QtProperty *>();
        }  else if (type == designerAlignmentTypeId()) {
            const uint align = Qt::AlignLeft | Qt::AlignVCenter;
            m_alignValues[property] = align;

            QtVariantProperty *alignH = addProperty(enumTypeId(), tr("Horizontal"));
            QStringList namesH;
            namesH << indexHToString(0) << indexHToString(1) << indexHToString(2) << indexHToString(3);
            alignH->setAttribute(QStringLiteral("enumNames"), namesH);
            alignH->setValue(alignToIndexH(align));
            m_propertyToAlignH[property] = alignH;
            m_alignHToProperty[alignH] = property;
            property->addSubProperty(alignH);

            QtVariantProperty *alignV = addProperty(enumTypeId(), tr("Vertical"));
            QStringList namesV;
            namesV << indexVToString(0) << indexVToString(1) << indexVToString(2);
            alignV->setAttribute(QStringLiteral("enumNames"), namesV);
            alignV->setValue(alignToIndexV(align));
            m_propertyToAlignV[property] = alignV;
            m_alignVToProperty[alignV] = property;
            property->addSubProperty(alignV);
        } else if (type == designerPixmapTypeId()) {
            m_pixmapValues[property] = PropertySheetPixmapValue();
            m_defaultPixmaps[property] = QPixmap();
        } else if (type == designerIconTypeId()) {
            m_iconValues[property] = PropertySheetIconValue();
            m_defaultIcons[property] = QIcon();

            QtVariantProperty *themeProp = addProperty(QMetaType::QString, tr("Theme"));
            themeProp->setAttribute(QLatin1String(themeAttributeC), true);
            m_iconSubPropertyToProperty[themeProp] = property;
            m_propertyToTheme[property] = themeProp;
            m_resetMap[themeProp] = true;
            property->addSubProperty(themeProp);

            createIconSubProperty(property, QIcon::Normal, QIcon::Off, tr("Normal Off"));
            createIconSubProperty(property, QIcon::Normal, QIcon::On, tr("Normal On"));
            createIconSubProperty(property, QIcon::Disabled, QIcon::Off, tr("Disabled Off"));
            createIconSubProperty(property, QIcon::Disabled, QIcon::On, tr("Disabled On"));
            createIconSubProperty(property, QIcon::Active, QIcon::Off, tr("Active Off"));
            createIconSubProperty(property, QIcon::Active, QIcon::On, tr("Active On"));
            createIconSubProperty(property, QIcon::Selected, QIcon::Off, tr("Selected Off"));
            createIconSubProperty(property, QIcon::Selected, QIcon::On, tr("Selected On"));
        } else if (type == designerStringTypeId()) {
            m_stringManager.initialize(this, property, PropertySheetStringValue());
            m_stringAttributes.insert(property, ValidationMultiLine);
            m_stringFontAttributes.insert(property, QApplication::font());
            m_stringThemeAttributes.insert(property, false);
        } else if (type == designerStringListTypeId()) {
            m_stringListManager.initialize(this, property, PropertySheetStringListValue());
        } else if (type == designerKeySequenceTypeId()) {
            m_keySequenceManager.initialize(this, property, PropertySheetKeySequenceValue());
        }
    }

    QtVariantPropertyManager::initializeProperty(property);
    m_fontManager.postInitializeProperty(this, property, type, DesignerPropertyManager::enumTypeId());
    if (type == QMetaType::Double)
        setAttribute(property, QStringLiteral("decimals"), 6);
}

void DesignerPropertyManager::createIconSubProperty(QtProperty *iconProperty, QIcon::Mode mode, QIcon::State state, const QString &subName)
{
    QPair<QIcon::Mode, QIcon::State> pair = qMakePair(mode, state);
    QtVariantProperty *subProp = addProperty(DesignerPropertyManager::designerPixmapTypeId(), subName);
    m_propertyToIconSubProperties[iconProperty][pair] = subProp;
    m_iconSubPropertyToState[subProp] = pair;
    m_iconSubPropertyToProperty[subProp] = iconProperty;
    m_resetMap[subProp] = true;
    iconProperty->addSubProperty(subProp);
}

void DesignerPropertyManager::uninitializeProperty(QtProperty *property)
{
    m_resetMap.remove(property);

    const auto propList = m_propertyToFlags.value(property);
    for (QtProperty *prop : propList) {
        if (prop) {
            delete prop;
            m_flagToProperty.remove(prop);
        }
    }
    m_propertyToFlags.remove(property);
    m_flagValues.remove(property);

    QtProperty *alignH = m_propertyToAlignH.value(property);
    if (alignH) {
        delete alignH;
        m_alignHToProperty.remove(alignH);
    }
    QtProperty *alignV = m_propertyToAlignV.value(property);
    if (alignV) {
        delete alignV;
        m_alignVToProperty.remove(alignV);
    }

    m_stringManager.uninitialize(property);
    m_stringListManager.uninitialize(property);
    m_keySequenceManager.uninitialize(property);

    if (QtProperty *iconTheme = m_propertyToTheme.value(property)) {
        delete iconTheme;
        m_iconSubPropertyToProperty.remove(iconTheme);
    }

    m_propertyToAlignH.remove(property);
    m_propertyToAlignV.remove(property);

    m_stringAttributes.remove(property);
    m_stringFontAttributes.remove(property);

    m_paletteValues.remove(property);

    m_iconValues.remove(property);
    m_defaultIcons.remove(property);

    m_pixmapValues.remove(property);
    m_defaultPixmaps.remove(property);

    QMap<QPair<QIcon::Mode, QIcon::State>, QtProperty *> iconSubProperties = m_propertyToIconSubProperties.value(property);
    for (auto itIcon = iconSubProperties.cbegin(), end = iconSubProperties.cend(); itIcon != end; ++itIcon) {
        QtProperty *subIcon = itIcon.value();
        delete subIcon;
        m_iconSubPropertyToState.remove(subIcon);
        m_iconSubPropertyToProperty.remove(subIcon);
    }
    m_propertyToIconSubProperties.remove(property);
    m_iconSubPropertyToState.remove(property);
    m_iconSubPropertyToProperty.remove(property);

    m_uintValues.remove(property);
    m_longLongValues.remove(property);
    m_uLongLongValues.remove(property);
    m_urlValues.remove(property);
    m_byteArrayValues.remove(property);

    m_fontManager.uninitializeProperty(property);
    m_brushManager.uninitializeProperty(property);

    QtVariantPropertyManager::uninitializeProperty(property);
}

bool DesignerPropertyManager::resetTextAlignmentProperty(QtProperty *property)
{
    const auto it = m_alignDefault.constFind(property);
    if (it == m_alignDefault.cend())
        return false;
    QtVariantProperty *alignProperty = variantProperty(property);
    alignProperty->setValue(DesignerPropertyManager::alignDefault(alignProperty));
    alignProperty->setModified(false);
    return true;
}

bool DesignerPropertyManager::resetFontSubProperty(QtProperty *property)
{
    return m_fontManager.resetFontSubProperty(this, property);
}

bool DesignerPropertyManager::resetIconSubProperty(QtProperty *property)
{
    QtProperty *iconProperty = m_iconSubPropertyToProperty.value(property);
    if (!iconProperty)
        return false;

    if (m_pixmapValues.contains(property)) {
        QtVariantProperty *pixmapProperty = variantProperty(property);
        pixmapProperty->setValue(QVariant::fromValue(PropertySheetPixmapValue()));
        return true;
    }
    if (m_propertyToTheme.contains(iconProperty)) {
        QtVariantProperty *themeProperty = variantProperty(property);
        themeProperty->setValue(QString());
        return true;
    }
    return false;
}

// -------- DesignerEditorFactory
DesignerEditorFactory::DesignerEditorFactory(QDesignerFormEditorInterface *core, QObject *parent) :
    QtVariantEditorFactory(parent),
    m_resetDecorator(new ResetDecorator(core, this)),
    m_changingPropertyValue(false),
    m_core(core),
    m_spacing(-1)
{
    connect(m_resetDecorator, &ResetDecorator::resetProperty,
            this, &DesignerEditorFactory::resetProperty);
}

DesignerEditorFactory::~DesignerEditorFactory() = default;

void DesignerEditorFactory::setSpacing(int spacing)
{
    m_spacing = spacing;
    m_resetDecorator->setSpacing(spacing);
}

void DesignerEditorFactory::setFormWindowBase(qdesigner_internal::FormWindowBase *fwb)
{
    m_fwb = fwb;
    DesignerPixmapCache *cache = nullptr;
    if (fwb)
        cache = fwb->pixmapCache();
    for (auto it = m_editorToPixmapProperty.cbegin(), end = m_editorToPixmapProperty.cend(); it != end; ++it)
        it.key()->setPixmapCache(cache);
    for (auto it = m_editorToIconProperty.cbegin(), end = m_editorToIconProperty.cend(); it != end; ++it)
        it.key()->setPixmapCache(cache);
}

void DesignerEditorFactory::connectPropertyManager(QtVariantPropertyManager *manager)
{
    m_resetDecorator->connectPropertyManager(manager);
    connect(manager, &QtVariantPropertyManager::attributeChanged,
                this, &DesignerEditorFactory::slotAttributeChanged);
    connect(manager, &QtVariantPropertyManager::valueChanged,
                this, &DesignerEditorFactory::slotValueChanged);
    connect(manager, &QtVariantPropertyManager::propertyChanged,
                this, &DesignerEditorFactory::slotPropertyChanged);
    QtVariantEditorFactory::connectPropertyManager(manager);
}

void DesignerEditorFactory::disconnectPropertyManager(QtVariantPropertyManager *manager)
{
    m_resetDecorator->disconnectPropertyManager(manager);
    disconnect(manager, &QtVariantPropertyManager::attributeChanged,
                this, &DesignerEditorFactory::slotAttributeChanged);
    disconnect(manager, &QtVariantPropertyManager::valueChanged,
                this, &DesignerEditorFactory::slotValueChanged);
    disconnect(manager, &QtVariantPropertyManager::propertyChanged,
                this, &DesignerEditorFactory::slotPropertyChanged);
    QtVariantEditorFactory::disconnectPropertyManager(manager);
}

// A helper that calls a setter with a value on a pointer list of editor objects.
// Could use QList<Editor*> instead of EditorContainer/Editor, but that crashes VS 6.
template <class EditorContainer, class Editor, class SetterParameter, class Value>
static inline void applyToEditors(const EditorContainer &list, void (Editor::*setter)(SetterParameter), const Value &value)
{
    if (list.isEmpty()) {
        return;
    }
    for (auto it = list.constBegin(), end = list.constEnd(); it != end; ++it) {
        Editor &editor = *(*it);
        (editor.*setter)(value);
    }
}

void DesignerEditorFactory::slotAttributeChanged(QtProperty *property, const QString &attribute, const QVariant &value)
{
    QtVariantPropertyManager *manager = propertyManager(property);
    const int type = manager->propertyType(property);
    if (type == DesignerPropertyManager::designerPixmapTypeId() && attribute == QLatin1String(defaultResourceAttributeC)) {
        const QPixmap pixmap = qvariant_cast<QPixmap>(value);
        applyToEditors(m_pixmapPropertyToEditors.value(property), &PixmapEditor::setDefaultPixmap, pixmap);
    } else if (type == DesignerPropertyManager::designerStringTypeId() || type == QMetaType::QString) {
        if (attribute == QLatin1String(validationModesAttributeC)) {
            const TextPropertyValidationMode validationMode = static_cast<TextPropertyValidationMode>(value.toInt());
            applyToEditors(m_stringPropertyToEditors.value(property), &TextEditor::setTextPropertyValidationMode, validationMode);
        }
        if (attribute == QLatin1String(fontAttributeC)) {
            const QFont font = qvariant_cast<QFont>(value);
            applyToEditors(m_stringPropertyToEditors.value(property), &TextEditor::setRichTextDefaultFont, font);
        }
        if (attribute == QLatin1String(themeAttributeC)) {
            const bool themeEnabled = value.toBool();
            applyToEditors(m_stringPropertyToEditors.value(property), &TextEditor::setIconThemeModeEnabled, themeEnabled);
        }
    } else if (type == QMetaType::QPalette && attribute == QLatin1String(superPaletteAttributeC)) {
        const QPalette palette = qvariant_cast<QPalette>(value);
        applyToEditors(m_palettePropertyToEditors.value(property), &PaletteEditorButton::setSuperPalette, palette);
    }
}

void DesignerEditorFactory::slotPropertyChanged(QtProperty *property)
{
    QtVariantPropertyManager *manager = propertyManager(property);
    const int type = manager->propertyType(property);
    if (type == DesignerPropertyManager::designerIconTypeId()) {
        QPixmap defaultPixmap;
        if (!property->isModified())
            defaultPixmap = qvariant_cast<QIcon>(manager->attributeValue(property, QLatin1String(defaultResourceAttributeC))).pixmap(16, 16);
        else if (m_fwb)
            defaultPixmap = m_fwb->iconCache()->icon(qvariant_cast<PropertySheetIconValue>(manager->value(property))).pixmap(16, 16);
        const auto editors = m_iconPropertyToEditors.value(property);
        for (PixmapEditor *editor : editors)
            editor->setDefaultPixmap(defaultPixmap);
    }
}

void DesignerEditorFactory::slotValueChanged(QtProperty *property, const QVariant &value)
{
    if (m_changingPropertyValue)
        return;

    QtVariantPropertyManager *manager = propertyManager(property);
    const int type = manager->propertyType(property);
    switch (type) {
    case QMetaType::QString:
        applyToEditors(m_stringPropertyToEditors.value(property), &TextEditor::setText, value.toString());
        break;
    case QMetaType::QPalette:
        applyToEditors(m_palettePropertyToEditors.value(property), &PaletteEditorButton::setPalette, qvariant_cast<QPalette>(value));
        break;
    case QMetaType::UInt:
        applyToEditors(m_uintPropertyToEditors.value(property), &QLineEdit::setText, QString::number(value.toUInt()));
        break;
    case QMetaType::LongLong:
        applyToEditors(m_longLongPropertyToEditors.value(property), &QLineEdit::setText, QString::number(value.toLongLong()));
        break;
    case QMetaType::ULongLong:
        applyToEditors(m_uLongLongPropertyToEditors.value(property), &QLineEdit::setText, QString::number(value.toULongLong()));
        break;
    case QMetaType::QUrl:
        applyToEditors(m_urlPropertyToEditors.value(property), &TextEditor::setText, value.toUrl().toString());
        break;
    case QMetaType::QByteArray:
        applyToEditors(m_byteArrayPropertyToEditors.value(property), &TextEditor::setText, QString::fromUtf8(value.toByteArray()));
        break;
    case QMetaType::QStringList:
        applyToEditors(m_stringListPropertyToEditors.value(property), &StringListEditorButton::setStringList, value.toStringList());
        break;
    default:
        if (type == DesignerPropertyManager::designerIconTypeId()) {
            PropertySheetIconValue iconValue = qvariant_cast<PropertySheetIconValue>(value);
            applyToEditors(m_iconPropertyToEditors.value(property), &PixmapEditor::setTheme, iconValue.theme());
            applyToEditors(m_iconPropertyToEditors.value(property), &PixmapEditor::setPath, iconValue.pixmap(QIcon::Normal, QIcon::Off).path());
        } else if (type == DesignerPropertyManager::designerPixmapTypeId()) {
            applyToEditors(m_pixmapPropertyToEditors.value(property), &PixmapEditor::setPath, qvariant_cast<PropertySheetPixmapValue>(value).path());
        } else if (type == DesignerPropertyManager::designerStringTypeId()) {
            applyToEditors(m_stringPropertyToEditors.value(property), &TextEditor::setText, qvariant_cast<PropertySheetStringValue>(value).value());
        } else if (type == DesignerPropertyManager::designerStringListTypeId()) {
            applyToEditors(m_stringListPropertyToEditors.value(property), &StringListEditorButton::setStringList, qvariant_cast<PropertySheetStringListValue>(value).value());
        } else if (type == DesignerPropertyManager::designerKeySequenceTypeId()) {
            applyToEditors(m_keySequencePropertyToEditors.value(property), &QKeySequenceEdit::setKeySequence, qvariant_cast<PropertySheetKeySequenceValue>(value).value());
        }
        break;
    }
}

TextEditor *DesignerEditorFactory::createTextEditor(QWidget *parent, TextPropertyValidationMode vm, const QString &value)
{
    TextEditor *rc = new TextEditor(m_core, parent);
    rc->setText(value);
    rc->setSpacing(m_spacing);
    rc->setTextPropertyValidationMode(vm);
    connect(rc, &QObject::destroyed, this, &DesignerEditorFactory::slotEditorDestroyed);
    return rc;
}

QWidget *DesignerEditorFactory::createEditor(QtVariantPropertyManager *manager, QtProperty *property,
            QWidget *parent)
{
    QWidget *editor = nullptr;
    const int type = manager->propertyType(property);
    switch (type) {
    case QMetaType::Bool: {
        editor = QtVariantEditorFactory::createEditor(manager, property, parent);
        QtBoolEdit *boolEdit = qobject_cast<QtBoolEdit *>(editor);
        if (boolEdit)
            boolEdit->setTextVisible(false);
    }
        break;
    case QMetaType::QString: {
        const TextPropertyValidationMode tvm = static_cast<TextPropertyValidationMode>(manager->attributeValue(property, QLatin1String(validationModesAttributeC)).toInt());
        TextEditor *ed = createTextEditor(parent, tvm, manager->value(property).toString());
        const QVariant richTextDefaultFont = manager->attributeValue(property, QLatin1String(fontAttributeC));
        if (richTextDefaultFont.metaType().id() == QMetaType::QFont)
            ed->setRichTextDefaultFont(qvariant_cast<QFont>(richTextDefaultFont));
        const bool themeEnabled = manager->attributeValue(property, QLatin1String(themeAttributeC)).toBool();
        ed->setIconThemeModeEnabled(themeEnabled);
        m_stringPropertyToEditors[property].append(ed);
        m_editorToStringProperty[ed] = property;
        connect(ed, &QObject::destroyed, this, &DesignerEditorFactory::slotEditorDestroyed);
        connect(ed, &TextEditor::textChanged, this, &DesignerEditorFactory::slotStringTextChanged);
        editor = ed;
    }
        break;
    case QMetaType::QPalette: {
        PaletteEditorButton *ed = new PaletteEditorButton(m_core, qvariant_cast<QPalette>(manager->value(property)), parent);
        ed->setSuperPalette(qvariant_cast<QPalette>(manager->attributeValue(property, QLatin1String(superPaletteAttributeC))));
        m_palettePropertyToEditors[property].append(ed);
        m_editorToPaletteProperty[ed] = property;
        connect(ed, &QObject::destroyed, this, &DesignerEditorFactory::slotEditorDestroyed);
        connect(ed, &PaletteEditorButton::paletteChanged, this, &DesignerEditorFactory::slotPaletteChanged);
        editor = ed;
    }
        break;
    case QMetaType::UInt: {
        QLineEdit *ed = new QLineEdit(parent);
        ed->setValidator(new QULongLongValidator(0, UINT_MAX, ed));
        ed->setText(QString::number(manager->value(property).toUInt()));
        m_uintPropertyToEditors[property].append(ed);
        m_editorToUintProperty[ed] = property;
        connect(ed, &QObject::destroyed, this, &DesignerEditorFactory::slotEditorDestroyed);
        connect(ed, &QLineEdit::textChanged, this, &DesignerEditorFactory::slotUintChanged);
        editor = ed;
    }
        break;
    case QMetaType::LongLong: {
        QLineEdit *ed = new QLineEdit(parent);
        ed->setValidator(new QLongLongValidator(ed));
        ed->setText(QString::number(manager->value(property).toLongLong()));
        m_longLongPropertyToEditors[property].append(ed);
        m_editorToLongLongProperty[ed] = property;
        connect(ed, &QObject::destroyed, this, &DesignerEditorFactory::slotEditorDestroyed);
        connect(ed, &QLineEdit::textChanged, this, &DesignerEditorFactory::slotLongLongChanged);
        editor = ed;
    }
        break;
    case QMetaType::ULongLong: {
        QLineEdit *ed = new QLineEdit(parent);
        ed->setValidator(new QULongLongValidator(ed));
        ed->setText(QString::number(manager->value(property).toULongLong()));
        m_uLongLongPropertyToEditors[property].append(ed);
        m_editorToULongLongProperty[ed] = property;
        connect(ed, &QObject::destroyed, this, &DesignerEditorFactory::slotEditorDestroyed);
        connect(ed, &QLineEdit::textChanged, this, &DesignerEditorFactory::slotULongLongChanged);
        editor = ed;
    }
        break;
    case QMetaType::QUrl: {
        TextEditor *ed = createTextEditor(parent, ValidationURL, manager->value(property).toUrl().toString());
        ed->setUpdateMode(TextPropertyEditor::UpdateOnFinished);
        m_urlPropertyToEditors[property].append(ed);
        m_editorToUrlProperty[ed] = property;
        connect(ed, &QObject::destroyed, this, &DesignerEditorFactory::slotEditorDestroyed);
        connect(ed, &TextEditor::textChanged, this, &DesignerEditorFactory::slotUrlChanged);
        editor = ed;
    }
        break;
    case QMetaType::QByteArray: {
        TextEditor *ed = createTextEditor(parent, ValidationMultiLine, QString::fromUtf8(manager->value(property).toByteArray()));
        m_byteArrayPropertyToEditors[property].append(ed);
        m_editorToByteArrayProperty[ed] = property;
        connect(ed, &QObject::destroyed, this, &DesignerEditorFactory::slotEditorDestroyed);
        connect(ed, &TextEditor::textChanged, this, &DesignerEditorFactory::slotByteArrayChanged);
        editor = ed;
    }
        break;
    default:
        if (type == DesignerPropertyManager::designerPixmapTypeId()) {
            PixmapEditor *ed = new PixmapEditor(m_core, parent);
            ed->setPixmapCache(m_fwb->pixmapCache());
            ed->setPath(qvariant_cast<PropertySheetPixmapValue>(manager->value(property)).path());
            ed->setDefaultPixmap(qvariant_cast<QPixmap>(manager->attributeValue(property, QLatin1String(defaultResourceAttributeC))));
            ed->setSpacing(m_spacing);
            m_pixmapPropertyToEditors[property].append(ed);
            m_editorToPixmapProperty[ed] = property;
            connect(ed, &QObject::destroyed, this, &DesignerEditorFactory::slotEditorDestroyed);
            connect(ed, &PixmapEditor::pathChanged, this, &DesignerEditorFactory::slotPixmapChanged);
            editor = ed;
        } else if (type == DesignerPropertyManager::designerIconTypeId()) {
            PixmapEditor *ed = new PixmapEditor(m_core, parent);
            ed->setPixmapCache(m_fwb->pixmapCache());
            ed->setIconThemeModeEnabled(true);
            PropertySheetIconValue value = qvariant_cast<PropertySheetIconValue>(manager->value(property));
            ed->setTheme(value.theme());
            ed->setPath(value.pixmap(QIcon::Normal, QIcon::Off).path());
            QPixmap defaultPixmap;
            if (!property->isModified())
                defaultPixmap = qvariant_cast<QIcon>(manager->attributeValue(property, QLatin1String(defaultResourceAttributeC))).pixmap(16, 16);
            else if (m_fwb)
                defaultPixmap = m_fwb->iconCache()->icon(value).pixmap(16, 16);
            ed->setDefaultPixmap(defaultPixmap);
            ed->setSpacing(m_spacing);
            m_iconPropertyToEditors[property].append(ed);
            m_editorToIconProperty[ed] = property;
            connect(ed, &QObject::destroyed, this, &DesignerEditorFactory::slotEditorDestroyed);
            connect(ed, &PixmapEditor::pathChanged, this, &DesignerEditorFactory::slotIconChanged);
            connect(ed, &PixmapEditor::themeChanged, this, &DesignerEditorFactory::slotIconThemeChanged);
            editor = ed;
        } else if (type == DesignerPropertyManager::designerStringTypeId()) {
            const TextPropertyValidationMode tvm = static_cast<TextPropertyValidationMode>(manager->attributeValue(property, QLatin1String(validationModesAttributeC)).toInt());
            TextEditor *ed = createTextEditor(parent, tvm, qvariant_cast<PropertySheetStringValue>(manager->value(property)).value());
            const QVariant richTextDefaultFont = manager->attributeValue(property, QLatin1String(fontAttributeC));
            if (richTextDefaultFont.metaType().id() == QMetaType::QFont)
                ed->setRichTextDefaultFont(qvariant_cast<QFont>(richTextDefaultFont));
            m_stringPropertyToEditors[property].append(ed);
            m_editorToStringProperty[ed] = property;
            connect(ed, &QObject::destroyed, this, &DesignerEditorFactory::slotEditorDestroyed);
            connect(ed, &TextEditor::textChanged, this, &DesignerEditorFactory::slotStringTextChanged);
            editor = ed;
        } else if (type == DesignerPropertyManager::designerStringListTypeId() || type == QMetaType::QStringList) {
            const QVariant variantValue = manager->value(property);
            const QStringList value = type == QMetaType::QStringList
                ? variantValue.toStringList() : qvariant_cast<PropertySheetStringListValue>(variantValue).value();
            StringListEditorButton *ed = new StringListEditorButton(value, parent);
            m_stringListPropertyToEditors[property].append(ed);
            m_editorToStringListProperty.insert(ed, property);
            connect(ed, &QObject::destroyed, this, &DesignerEditorFactory::slotEditorDestroyed);
            connect(ed, &StringListEditorButton::stringListChanged, this, &DesignerEditorFactory::slotStringListChanged);
            editor = ed;
        } else if (type == DesignerPropertyManager::designerKeySequenceTypeId()) {
            QKeySequenceEdit *ed = new QKeySequenceEdit(parent);
            ed->setKeySequence(qvariant_cast<PropertySheetKeySequenceValue>(manager->value(property)).value());
            m_keySequencePropertyToEditors[property].append(ed);
            m_editorToKeySequenceProperty[ed] = property;
            connect(ed, &QObject::destroyed, this, &DesignerEditorFactory::slotEditorDestroyed);
            connect(ed, &QKeySequenceEdit::keySequenceChanged, this, &DesignerEditorFactory::slotKeySequenceChanged);
            editor = ed;
        } else {
            editor = QtVariantEditorFactory::createEditor(manager, property, parent);
        }
        break;
    }
    return m_resetDecorator->editor(editor,
            manager->variantProperty(property)->attributeValue(QLatin1String(resettableAttributeC)).toBool(),
            manager, property, parent);
}

template <class Editor>
bool removeEditor(QObject *object,
                QMap<QtProperty *, QList<Editor> > *propertyToEditors,
                QMap<Editor, QtProperty *> *editorToProperty)
{
    if (!propertyToEditors)
        return false;
    if (!editorToProperty)
        return false;
    for (auto e2pIt = editorToProperty->begin(), end = editorToProperty->end(); e2pIt != end; ++e2pIt) {
        Editor editor = e2pIt.key();
        if (editor == object) {
            const auto p2eIt = propertyToEditors->find(e2pIt.value());
            if (p2eIt != propertyToEditors->end()) {
                p2eIt.value().removeAll(editor);
                if (p2eIt.value().isEmpty())
                    propertyToEditors->erase(p2eIt);
            }
            editorToProperty->erase(e2pIt);
            return true;
        }
    }
    return false;
}

void DesignerEditorFactory::slotEditorDestroyed(QObject *object)
{
    if (removeEditor(object, &m_stringPropertyToEditors, &m_editorToStringProperty))
        return;
    if (removeEditor(object, &m_keySequencePropertyToEditors, &m_editorToKeySequenceProperty))
        return;
    if (removeEditor(object, &m_palettePropertyToEditors, &m_editorToPaletteProperty))
        return;
    if (removeEditor(object, &m_pixmapPropertyToEditors, &m_editorToPixmapProperty))
        return;
    if (removeEditor(object, &m_iconPropertyToEditors, &m_editorToIconProperty))
        return;
    if (removeEditor(object, &m_uintPropertyToEditors, &m_editorToUintProperty))
        return;
    if (removeEditor(object, &m_longLongPropertyToEditors, &m_editorToLongLongProperty))
        return;
    if (removeEditor(object, &m_uLongLongPropertyToEditors, &m_editorToULongLongProperty))
        return;
    if (removeEditor(object, &m_urlPropertyToEditors, &m_editorToUrlProperty))
        return;
    if (removeEditor(object, &m_byteArrayPropertyToEditors, &m_editorToByteArrayProperty))
        return;
    if (removeEditor(object, &m_stringListPropertyToEditors, &m_editorToStringListProperty))
        return;
}

template<class Editor>
bool updateManager(QtVariantEditorFactory *factory, bool *changingPropertyValue,
        const QMap<Editor, QtProperty *> &editorToProperty, QWidget *editor, const QVariant &value)
{
    if (!editor)
        return false;
    for (auto it = editorToProperty.cbegin(), end = editorToProperty.cend(); it != end; ++it) {
        if (it.key() == editor) {
            QtProperty *prop = it.value();
            QtVariantPropertyManager *manager = factory->propertyManager(prop);
            *changingPropertyValue = true;
            manager->variantProperty(prop)->setValue(value);
            *changingPropertyValue = false;
            return true;
        }
    }
    return false;
}

void DesignerEditorFactory::slotUintChanged(const QString &value)
{
    updateManager(this, &m_changingPropertyValue, m_editorToUintProperty, qobject_cast<QWidget *>(sender()), value.toUInt());
}

void DesignerEditorFactory::slotLongLongChanged(const QString &value)
{
    updateManager(this, &m_changingPropertyValue, m_editorToLongLongProperty, qobject_cast<QWidget *>(sender()), value.toLongLong());
}

void DesignerEditorFactory::slotULongLongChanged(const QString &value)
{
    updateManager(this, &m_changingPropertyValue, m_editorToULongLongProperty, qobject_cast<QWidget *>(sender()), value.toULongLong());
}

void DesignerEditorFactory::slotUrlChanged(const QString &value)
{
    updateManager(this, &m_changingPropertyValue, m_editorToUrlProperty, qobject_cast<QWidget *>(sender()), QUrl(value));
}

void DesignerEditorFactory::slotByteArrayChanged(const QString &value)
{
    updateManager(this, &m_changingPropertyValue, m_editorToByteArrayProperty, qobject_cast<QWidget *>(sender()), value.toUtf8());
}

template <class Editor>
QtProperty *findPropertyForEditor(const QMap<Editor *, QtProperty *> &editorMap,
                                  const QObject *sender)
{
    for (auto it = editorMap.constBegin(), cend = editorMap.constEnd(); it != cend; ++it)
        if (it.key() == sender)
            return it.value();
    return nullptr;
}

void DesignerEditorFactory::slotStringTextChanged(const QString &value)
{
    if (QtProperty *prop = findPropertyForEditor(m_editorToStringProperty, sender())) {
        QtVariantPropertyManager *manager = propertyManager(prop);
        QtVariantProperty *varProp = manager->variantProperty(prop);
        QVariant val = varProp->value();
        if (val.userType() == DesignerPropertyManager::designerStringTypeId()) {
            PropertySheetStringValue strVal = qvariant_cast<PropertySheetStringValue>(val);
            strVal.setValue(value);
            // Disable translation if no translation subproperties exist.
            if (varProp->subProperties().isEmpty())
                strVal.setTranslatable(false);
            val = QVariant::fromValue(strVal);
        } else {
            val = QVariant(value);
        }
        m_changingPropertyValue = true;
        manager->variantProperty(prop)->setValue(val);
        m_changingPropertyValue = false;
    }
}

void DesignerEditorFactory::slotKeySequenceChanged(const QKeySequence &value)
{
    if (QtProperty *prop = findPropertyForEditor(m_editorToKeySequenceProperty, sender())) {
        QtVariantPropertyManager *manager = propertyManager(prop);
        QtVariantProperty *varProp = manager->variantProperty(prop);
        QVariant val = varProp->value();
        if (val.userType() == DesignerPropertyManager::designerKeySequenceTypeId()) {
            PropertySheetKeySequenceValue keyVal = qvariant_cast<PropertySheetKeySequenceValue>(val);
            keyVal.setValue(value);
            val = QVariant::fromValue(keyVal);
        } else {
            val = QVariant::fromValue(value);
        }
        m_changingPropertyValue = true;
        manager->variantProperty(prop)->setValue(val);
        m_changingPropertyValue = false;
    }
}

void DesignerEditorFactory::slotPaletteChanged(const QPalette &value)
{
    updateManager(this, &m_changingPropertyValue, m_editorToPaletteProperty, qobject_cast<QWidget *>(sender()), QVariant::fromValue(value));
}

void DesignerEditorFactory::slotPixmapChanged(const QString &value)
{
    updateManager(this, &m_changingPropertyValue, m_editorToPixmapProperty, qobject_cast<QWidget *>(sender()),
                    QVariant::fromValue(PropertySheetPixmapValue(value)));
}

void DesignerEditorFactory::slotIconChanged(const QString &value)
{
    updateManager(this, &m_changingPropertyValue, m_editorToIconProperty, qobject_cast<QWidget *>(sender()),
                    QVariant::fromValue(PropertySheetIconValue(PropertySheetPixmapValue(value))));
}

void DesignerEditorFactory::slotIconThemeChanged(const QString &value)
{
    PropertySheetIconValue icon;
    icon.setTheme(value);
    updateManager(this, &m_changingPropertyValue, m_editorToIconProperty, qobject_cast<QWidget *>(sender()),
                    QVariant::fromValue(icon));
}

void DesignerEditorFactory::slotStringListChanged(const QStringList &value)
{
    if (QtProperty *prop = findPropertyForEditor(m_editorToStringListProperty, sender())) {
        QtVariantPropertyManager *manager = propertyManager(prop);
        QtVariantProperty *varProp = manager->variantProperty(prop);
        QVariant val = varProp->value();
        if (val.userType() == DesignerPropertyManager::designerStringListTypeId()) {
            PropertySheetStringListValue listValue = qvariant_cast<PropertySheetStringListValue>(val);
            listValue.setValue(value);
            // Disable translation if no translation subproperties exist.
            if (varProp->subProperties().isEmpty())
                listValue.setTranslatable(false);
            val = QVariant::fromValue(listValue);
        } else {
            val = QVariant(value);
        }
        m_changingPropertyValue = true;
        manager->variantProperty(prop)->setValue(val);
        m_changingPropertyValue = false;
    }
}

ResetDecorator::ResetDecorator(const QDesignerFormEditorInterface *core, QObject *parent)
    : QObject(parent)
    , m_spacing(-1)
    , m_core(core)
{
}

ResetDecorator::~ResetDecorator()
{
    const auto editors = m_resetWidgetToProperty.keys();
    qDeleteAll(editors);
}

void ResetDecorator::connectPropertyManager(QtAbstractPropertyManager *manager)
{
    connect(manager, &QtAbstractPropertyManager::propertyChanged,
            this, &ResetDecorator::slotPropertyChanged);
}

void ResetDecorator::disconnectPropertyManager(QtAbstractPropertyManager *manager)
{
    disconnect(manager, &QtAbstractPropertyManager::propertyChanged,
            this, &ResetDecorator::slotPropertyChanged);
}

void ResetDecorator::setSpacing(int spacing)
{
    m_spacing = spacing;
}

static inline bool isModifiedInMultiSelection(const QDesignerFormEditorInterface *core,
                                              const QString &propertyName)
{
    const QDesignerFormWindowInterface *form = core->formWindowManager()->activeFormWindow();
    if (!form)
        return false;
    const QDesignerFormWindowCursorInterface *cursor = form->cursor();
    const int selectionSize = cursor->selectedWidgetCount();
    if (selectionSize < 2)
        return false;
    for (int i = 0; i < selectionSize; ++i) {
        const QDesignerPropertySheetExtension *sheet =
            qt_extension<QDesignerPropertySheetExtension*>(core->extensionManager(),
                                                           cursor->selectedWidget(i));
        const int index = sheet->indexOf(propertyName);
        if (index >= 0 && sheet->isChanged(index))
            return true;
    }
    return false;
}

QWidget *ResetDecorator::editor(QWidget *subEditor, bool resettable, QtAbstractPropertyManager *manager, QtProperty *property,
            QWidget *parent)
{
    Q_UNUSED(manager);

    ResetWidget *resetWidget = nullptr;
    if (resettable) {
        resetWidget = new ResetWidget(property, parent);
        resetWidget->setSpacing(m_spacing);
        resetWidget->setResetEnabled(property->isModified() || isModifiedInMultiSelection(m_core, property->propertyName()));
        resetWidget->setValueText(property->valueText());
        resetWidget->setValueIcon(property->valueIcon());
        resetWidget->setAutoFillBackground(true);
        connect(resetWidget, &QObject::destroyed, this, &ResetDecorator::slotEditorDestroyed);
        connect(resetWidget, &ResetWidget::resetProperty, this, &ResetDecorator::resetProperty);
        m_createdResetWidgets[property].append(resetWidget);
        m_resetWidgetToProperty[resetWidget] = property;
    }
    if (subEditor) {
        if (resetWidget) {
            subEditor->setParent(resetWidget);
            resetWidget->setWidget(subEditor);
        }
    }
    if (resetWidget)
        return resetWidget;
    return subEditor;
}

void ResetDecorator::slotPropertyChanged(QtProperty *property)
{
    const auto prIt = m_createdResetWidgets.constFind(property);
    if (prIt == m_createdResetWidgets.constEnd())
        return;

    for (ResetWidget *widget : prIt.value()) {
        widget->setResetEnabled(property->isModified() || isModifiedInMultiSelection(m_core, property->propertyName()));
        widget->setValueText(property->valueText());
        widget->setValueIcon(property->valueIcon());
    }
}

void ResetDecorator::slotEditorDestroyed(QObject *object)
{
    const  QMap<ResetWidget *, QtProperty *>::ConstIterator rcend = m_resetWidgetToProperty.constEnd();
    for (QMap<ResetWidget *, QtProperty *>::ConstIterator itEditor =  m_resetWidgetToProperty.constBegin(); itEditor != rcend; ++itEditor) {
        if (itEditor.key() == object) {
            ResetWidget *editor = itEditor.key();
            QtProperty *property = itEditor.value();
            m_resetWidgetToProperty.remove(editor);
            m_createdResetWidgets[property].removeAll(editor);
            if (m_createdResetWidgets[property].isEmpty())
                m_createdResetWidgets.remove(property);
            return;
        }
    }
}

}

QT_END_NAMESPACE

#include "designerpropertymanager.moc"
