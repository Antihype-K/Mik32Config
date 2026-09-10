#include "screen_navigator.h"

ScreenNavigator::ScreenNavigator(NavState initial)
    : state_(std::move(initial))
{
}

void ScreenNavigator::goTo(Screen screen)
{
    state_.screen = screen;
}

void ScreenNavigator::setDiagnosticErrors(int count)
{
    state_.diagnosticErrors = count;
}

void ScreenNavigator::setReverseImportActive(bool value)
{
    state_.reverseImportActive = value;
}

void ScreenNavigator::setGeneratedOnce(bool value)
{
    state_.hasGeneratedOnce = value;
}

void ScreenNavigator::setActiveProjectName(const QString& name)
{
    state_.activeProjectName = name;
}

NavViewModel ScreenNavigator::viewModel() const
{
    NavViewModel vm;

    switch (state_.screen)
    {
        case Screen::Home:           vm.stackIndex = 0; break;
        case Screen::NewProject:     vm.stackIndex = 1; break;
        case Screen::Config:         vm.stackIndex = 2; break;
        case Screen::ProjectManager: vm.stackIndex = 3; break;
        case Screen::CodePreview:    vm.stackIndex = 4; break;
    }

    // Верхняя строка и вкладки видны только там, где есть открытый проект,
    // с которым можно работать (Config/ProjectManager/CodePreview) —
    // на Home и NewProject проекта ещё нет, показывать кнопку «Сгенерировать»
    // или вкладки было бы нечего.
    const bool in_workspace = state_.screen == Screen::Config
        || state_.screen == Screen::ProjectManager
        || state_.screen == Screen::CodePreview;

    vm.topBarVisible = in_workspace;
    vm.tabStripVisible = in_workspace;
    vm.generateButtonVisible = in_workspace;
    vm.generateButtonEnabled = in_workspace && (state_.diagnosticErrors == 0 || state_.reverseImportActive);
    vm.codeTabEnabled = state_.hasGeneratedOnce;

    switch (state_.screen)
    {
        case Screen::Config:         vm.activeTabIndex = 0; break;
        case Screen::ProjectManager: vm.activeTabIndex = 1; break;
        case Screen::CodePreview:    vm.activeTabIndex = 2; break;
        default:                     vm.activeTabIndex = -1; break;
    }

    switch (state_.screen)
    {
        case Screen::Home:
            vm.breadcrumbText = QString::fromUtf8("Главная");
            break;
        case Screen::NewProject:
            vm.breadcrumbText = QString::fromUtf8("Главная › Новый проект");
            break;
        case Screen::Config:
            vm.breadcrumbText = QString::fromUtf8("Главная › %1 › Pinout & Configuration")
                .arg(state_.activeProjectName);
            break;
        case Screen::ProjectManager:
            vm.breadcrumbText = QString::fromUtf8("Главная › %1 › Project Manager")
                .arg(state_.activeProjectName);
            break;
        case Screen::CodePreview:
            vm.breadcrumbText = QString::fromUtf8("Главная › %1 › Просмотр кода")
                .arg(state_.activeProjectName);
            break;
    }

    return vm;
}
