"""Real LVGL Params controls: pointer fences, local-only choices, retirement geometry."""

import test_action_native_input as action


class ActionParametersNativeInputTests(action.ActionNativeInputTests):
    extra_features = ("ACTION_PARAMETERS",)

    def test_profiles_observation_and_pointer_retirement(self):
        cases = (
            *action.SCENARIOS,
            "params-choice",
            "params-disabled-appearance",
            "params-default-false",
            "params-default-true",
            "params-stop",
            "params-overflow-stop",
            "params-aba",
            "params-semantic",
            "params-telemetry",
            "params-outside",
            "params-unfit",
            "params-retired",
        )
        for case in cases:
            with self.subTest(case=case):
                self.assertIn(
                    "RESULT " + case + " pass=1",
                    self.checked([self.executable, case]).stdout,
                )
