"""AnimForge WarpViz - Maya PySide UI.

The dialog the animator drives:

    1. set the evaluation time range (defaults to the playback range),
    2. pick a RootMotionWarpingMethod (SkewWarp, SimpleWarp, Scale),
    3. pick the Warp Target locator ("Use Selected"),
    4. press Evaluate.

Evaluate delegates to the compiled AnimForgeMayaWarpViz.mll via the
`animForgeWarpViz` command; the plugin talks to the Unreal gym, and when the
result lands it builds the "AnimForgeWarpViz_Result" anim layer with the
warped trajectory curve and ghost pose locators. This file is deliberately
thin: all decision logic lives in warpviz_session.py so it can be unit-tested
without Qt or Maya.

Launch from Maya:

    import animforge_warpviz_ui
    animforge_warpviz_ui.show()
"""

from __future__ import annotations

import os
import sys

# Maya 2025+ ships PySide6; earlier versions PySide2.
try:
    from PySide6 import QtCore, QtWidgets  # type: ignore
except ImportError:  # pragma: no cover - depends on Maya version
    from PySide2 import QtCore, QtWidgets  # type: ignore

try:
    import maya.cmds as cmds
    import maya.OpenMayaUI as omui
    _IN_MAYA = True
except ImportError:  # allows importing this module outside Maya for tooling
    cmds = None
    omui = None
    _IN_MAYA = False

_SCRIPTS_DIR = os.path.dirname(os.path.abspath(__file__))
if _SCRIPTS_DIR not in sys.path:
    sys.path.insert(0, _SCRIPTS_DIR)

from warpviz_protocol import DEFAULT_PORT, WARP_METHODS
from warpviz_session import SessionSettings, build_evaluate_command, validate_settings

_PLUGIN_NAME = "AnimForgeMayaWarpViz"
_DIALOG_OBJECT_NAME = "AnimForgeWarpVizDialog"

_dialog_instance = None


def _maya_main_window():
    """Returns Maya's main window as a QWidget parent (None outside Maya)."""
    if not _IN_MAYA:
        return None
    try:
        from shiboken6 import wrapInstance  # type: ignore
    except ImportError:
        from shiboken2 import wrapInstance  # type: ignore
    pointer = omui.MQtUtil.mainWindow()
    if pointer is None:
        return None
    return wrapInstance(int(pointer), QtWidgets.QWidget)


class WarpVizDialog(QtWidgets.QDialog):

    def __init__(self, parent=None):
        super(WarpVizDialog, self).__init__(parent or _maya_main_window())
        self.setObjectName(_DIALOG_OBJECT_NAME)
        self.setWindowTitle("AnimForge WarpViz - Motion Warp Evaluate")
        self.setMinimumWidth(420)
        self.setWindowFlags(self.windowFlags() & ~QtCore.Qt.WindowContextHelpButtonHint)

        self._build_widgets()
        self._build_layout()
        self._connect_signals()
        self._populate_from_scene()

    # ------------------------------------------------------------------
    # UI construction
    # ------------------------------------------------------------------

    def _build_widgets(self):
        # -- connection --------------------------------------------------
        self.host_field = QtWidgets.QLineEdit("127.0.0.1")
        self.port_field = QtWidgets.QSpinBox()
        self.port_field.setRange(1, 65535)
        self.port_field.setValue(DEFAULT_PORT)
        self.connect_button = QtWidgets.QPushButton("Connect")

        # -- character / clip ---------------------------------------------
        self.character_field = QtWidgets.QLineEdit()
        self.character_field.setPlaceholderText("e.g. AF_Mannequin")
        self.clip_field = QtWidgets.QLineEdit()
        self.clip_field.setPlaceholderText("clip name registered in the gym, e.g. MM_Vault_Low")

        # -- time range ----------------------------------------------------
        self.start_frame_field = QtWidgets.QDoubleSpinBox()
        self.end_frame_field = QtWidgets.QDoubleSpinBox()
        for spin in (self.start_frame_field, self.end_frame_field):
            spin.setRange(-100000.0, 100000.0)
            spin.setDecimals(1)

        self.use_window_checkbox = QtWidgets.QCheckBox("Custom warp window")
        self.window_start_field = QtWidgets.QDoubleSpinBox()
        self.window_end_field = QtWidgets.QDoubleSpinBox()
        for spin in (self.window_start_field, self.window_end_field):
            spin.setRange(-100000.0, 100000.0)
            spin.setDecimals(1)
            spin.setEnabled(False)

        # -- warp settings ---------------------------------------------------
        self.method_combo = QtWidgets.QComboBox()
        self.method_combo.addItems(list(WARP_METHODS))
        self.target_field = QtWidgets.QLineEdit()
        self.target_field.setPlaceholderText("warp target locator")
        self.use_selected_button = QtWidgets.QPushButton("Use Selected")
        self.warp_translation_checkbox = QtWidgets.QCheckBox("Warp Translation")
        self.warp_translation_checkbox.setChecked(True)
        self.warp_rotation_checkbox = QtWidgets.QCheckBox("Warp Rotation")
        self.warp_rotation_checkbox.setChecked(True)
        self.ghost_interval_field = QtWidgets.QDoubleSpinBox()
        self.ghost_interval_field.setRange(0.0, 1000.0)
        self.ghost_interval_field.setValue(5.0)
        self.ghost_interval_field.setSuffix(" frames")
        self.ghost_interval_field.setToolTip("Ghost pose sampling stride; 0 disables ghosts")

        # -- actions ---------------------------------------------------------
        self.evaluate_button = QtWidgets.QPushButton("Evaluate")
        self.evaluate_button.setDefault(True)
        self.status_label = QtWidgets.QLabel("Idle.")
        self.status_label.setWordWrap(True)

    def _build_layout(self):
        connection_group = QtWidgets.QGroupBox("Unreal Gym Connection")
        connection_layout = QtWidgets.QHBoxLayout(connection_group)
        connection_layout.addWidget(QtWidgets.QLabel("Host"))
        connection_layout.addWidget(self.host_field, 1)
        connection_layout.addWidget(QtWidgets.QLabel("Port"))
        connection_layout.addWidget(self.port_field)
        connection_layout.addWidget(self.connect_button)

        character_group = QtWidgets.QGroupBox("Character / Clip")
        character_form = QtWidgets.QFormLayout(character_group)
        character_form.addRow("Character ID", self.character_field)
        character_form.addRow("Clip Name", self.clip_field)

        range_group = QtWidgets.QGroupBox("Time Range")
        range_form = QtWidgets.QGridLayout(range_group)
        range_form.addWidget(QtWidgets.QLabel("Start"), 0, 0)
        range_form.addWidget(self.start_frame_field, 0, 1)
        range_form.addWidget(QtWidgets.QLabel("End"), 0, 2)
        range_form.addWidget(self.end_frame_field, 0, 3)
        range_form.addWidget(self.use_window_checkbox, 1, 0, 1, 4)
        range_form.addWidget(QtWidgets.QLabel("Window Start"), 2, 0)
        range_form.addWidget(self.window_start_field, 2, 1)
        range_form.addWidget(QtWidgets.QLabel("Window End"), 2, 2)
        range_form.addWidget(self.window_end_field, 2, 3)

        warp_group = QtWidgets.QGroupBox("Root Motion Warping")
        warp_form = QtWidgets.QGridLayout(warp_group)
        warp_form.addWidget(QtWidgets.QLabel("Method"), 0, 0)
        warp_form.addWidget(self.method_combo, 0, 1, 1, 2)
        warp_form.addWidget(QtWidgets.QLabel("Warp Target"), 1, 0)
        warp_form.addWidget(self.target_field, 1, 1)
        warp_form.addWidget(self.use_selected_button, 1, 2)
        warp_form.addWidget(self.warp_translation_checkbox, 2, 0, 1, 2)
        warp_form.addWidget(self.warp_rotation_checkbox, 2, 2)
        warp_form.addWidget(QtWidgets.QLabel("Ghost Interval"), 3, 0)
        warp_form.addWidget(self.ghost_interval_field, 3, 1)

        main_layout = QtWidgets.QVBoxLayout(self)
        main_layout.addWidget(connection_group)
        main_layout.addWidget(character_group)
        main_layout.addWidget(range_group)
        main_layout.addWidget(warp_group)
        main_layout.addWidget(self.evaluate_button)
        main_layout.addWidget(self.status_label)

    def _connect_signals(self):
        self.connect_button.clicked.connect(self._on_connect)
        self.use_selected_button.clicked.connect(self._on_use_selected)
        self.evaluate_button.clicked.connect(self._on_evaluate)
        self.use_window_checkbox.toggled.connect(self.window_start_field.setEnabled)
        self.use_window_checkbox.toggled.connect(self.window_end_field.setEnabled)

    # ------------------------------------------------------------------
    # Scene interaction
    # ------------------------------------------------------------------

    def _populate_from_scene(self):
        if not _IN_MAYA:
            return
        start = cmds.playbackOptions(query=True, minTime=True)
        end = cmds.playbackOptions(query=True, maxTime=True)
        self.start_frame_field.setValue(start)
        self.end_frame_field.setValue(end)
        self.window_start_field.setValue(start)
        self.window_end_field.setValue(end)

    def _current_fps(self):
        if not _IN_MAYA:
            return 30.0
        unit = cmds.currentUnit(query=True, time=True)
        fps_by_unit = {
            "game": 15.0, "film": 24.0, "pal": 25.0, "ntsc": 30.0,
            "show": 48.0, "palf": 50.0, "ntscf": 60.0,
        }
        if unit in fps_by_unit:
            return fps_by_unit[unit]
        if unit.endswith("fps"):
            try:
                return float(unit[:-3])
            except ValueError:
                pass
        return 30.0

    def _on_use_selected(self):
        if not _IN_MAYA:
            return
        selection = cmds.ls(selection=True, transforms=True) or []
        if not selection:
            self._set_status("Select a locator to use as the warp target.", error=True)
            return
        self.target_field.setText(selection[0])
        self._set_status("Warp target: %s" % selection[0])

    # ------------------------------------------------------------------
    # Actions
    # ------------------------------------------------------------------

    def _gather_settings(self):
        window = None
        if self.use_window_checkbox.isChecked():
            window = (self.window_start_field.value(), self.window_end_field.value())
        return SessionSettings(
            host=self.host_field.text().strip(),
            port=self.port_field.value(),
            character_id=self.character_field.text().strip(),
            clip_name=self.clip_field.text().strip(),
            start_frame=self.start_frame_field.value(),
            end_frame=self.end_frame_field.value(),
            fps=self._current_fps(),
            warp_method=self.method_combo.currentText(),
            warp_target_locator=self.target_field.text().strip(),
            warp_rotation=self.warp_rotation_checkbox.isChecked(),
            warp_translation=self.warp_translation_checkbox.isChecked(),
            ghost_interval_frames=self.ghost_interval_field.value(),
            warp_window=window,
        )

    def _ensure_plugin_loaded(self):
        if not _IN_MAYA:
            self._set_status("Not running inside Maya.", error=True)
            return False
        if not cmds.pluginInfo(_PLUGIN_NAME, query=True, loaded=True):
            try:
                cmds.loadPlugin(_PLUGIN_NAME)
            except RuntimeError:
                self._set_status(
                    "%s.mll is not available. Build it via BuildMayaPlugin.bat and add it "
                    "to MAYA_PLUG_IN_PATH." % _PLUGIN_NAME, error=True)
                return False
        return True

    def _on_connect(self):
        settings = self._gather_settings()
        if not self._ensure_plugin_loaded():
            return
        try:
            import maya.mel as mel
            mel.eval('animForgeWarpViz -connect -host "%s" -port %d -characterId "%s";'
                     % (settings.host, settings.port, settings.character_id))
            self._set_status("Connected to gym at %s:%d." % (settings.host, settings.port))
        except RuntimeError as exc:
            self._set_status("Connect failed: %s" % exc, error=True)

    def _on_evaluate(self):
        settings = self._gather_settings()
        validation = validate_settings(settings)
        if not validation.ok:
            self._set_status("\n".join(validation.errors), error=True)
            return
        for warning in validation.warnings:
            self._set_status(warning)

        if not self._ensure_plugin_loaded():
            return

        command = build_evaluate_command(settings)
        try:
            import maya.mel as mel
            mel.eval(command)
            self._set_status(
                "Evaluate sent (%s, frames %g-%g). The result anim layer appears when the gym "
                "answers." % (settings.warp_method, settings.start_frame, settings.end_frame))
        except RuntimeError as exc:
            self._set_status("Evaluate failed: %s" % exc, error=True)

    def _set_status(self, message, error=False):
        self.status_label.setStyleSheet("color: %s;" % ("#e05c5c" if error else "#9acd6a"))
        self.status_label.setText(message)


def show():
    """Shows the dialog (singleton per Maya session)."""
    global _dialog_instance
    if _dialog_instance is not None:
        try:
            _dialog_instance.close()
            _dialog_instance.deleteLater()
        except RuntimeError:
            pass
    _dialog_instance = WarpVizDialog()
    _dialog_instance.show()
    return _dialog_instance
